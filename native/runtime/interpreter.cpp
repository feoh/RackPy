// SPDX-License-Identifier: GPL-3.0-or-later
// Promoted from the RP-04 spike host (ADR 002, decisions 5 and 6) and the RP-06
// lifecycle probe's teardown sentinel (ADR 003, decision 13).
#include "rackpy/runtime/interpreter.hpp"

#include <cstdio>
#include <vector>

#include "rackpy/runtime/diagnostics.hpp"
#include "rackpy/runtime/python.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace runtime {

namespace {

/** Saved by PyEval_SaveThread() when start-up ends; non-null while the interpreter is ours to finalize. */
PyThreadState* mainThreadState = nullptr;
bool shutdownBegun = false;
std::string outcome = "not started";
std::vector<std::function<void()>> detachers;

void setError(RackPyHostInit* args, const std::string& msg) {
	if (args->error && args->error_size)
		std::snprintf(args->error, args->error_size, "%s", msg.c_str());
}

std::string statusMessage(const char* what, PyStatus status) {
	return std::string(what) + ": " + (status.err_msg ? status.err_msg : "unknown error");
}

/** Isolated configuration: no site, no user site, no environment, no argv, no signal handlers. */
bool initializeInterpreter(RackPyHostInit* args) {
	PyPreConfig preconfig;
	PyPreConfig_InitIsolatedConfig(&preconfig);
	preconfig.utf8_mode = 1;
	PyStatus status = Py_PreInitialize(&preconfig);
	if (PyStatus_Exception(status)) {
		setError(args, statusMessage("Py_PreInitialize", status));
		return false;
	}

	PyConfig config;
	PyConfig_InitIsolatedConfig(&config);
	config.install_signal_handlers = 0;  // Rack owns signal handling
	config.site_import = 0;
	config.user_site_directory = 0;
	config.write_bytecode = 0;  // never write into the installed plugin
	config.configure_c_stdio = 0;
	config.parse_argv = 0;
	config.pathconfig_warnings = 0;
	std::string program = std::string(args->plugin_dir) + "/rackpy";
	const char* failure = nullptr;
	status = PyConfig_SetBytesString(&config, &config.home, args->python_home);
	if (PyStatus_Exception(status))
		failure = "PyConfig home";
	if (!failure) {
		status = PyConfig_SetBytesString(&config, &config.program_name, program.c_str());
		if (PyStatus_Exception(status))
			failure = "PyConfig program_name";
	}
	if (!failure) {
		status = Py_InitializeFromConfig(&config);
		if (PyStatus_Exception(status))
			failure = "Py_InitializeFromConfig";
	}
	PyConfig_Clear(&config);
	if (failure) {
		// Never Py_ExitStatusException(): it would exit Rack.
		setError(args, statusMessage(failure, status));
		return false;
	}
	return true;
}

/** Call a rackpy._bootstrap hook with the GIL held; report, never propagate, a failure. */
bool bootstrapHook(const char* name) {
	try {
		py::module_::import("rackpy._bootstrap").attr(name)();
		return true;
	}
	catch (const std::exception& e) {
		WARN("RackPy: rackpy._bootstrap.%s raised: %s", name, e.what());
		return false;
	}
}

/** A widget in the bound context's scene whose teardown is the earliest signal Rack gives a plugin. */
struct Sentinel : rack::widget::Widget {
	void step() override {
		Widget::step();
		// Python references dropped on engine threads are released here, on the UI thread.
		if (pendingReleases())
			callPython("deferred releases", [] {});
	}
	void onContextDestroy(const ContextDestroyEvent& e) override {
		beginContextShutdown("window closing");  // windowed: the window is deleted first
		Widget::onContextDestroy(e);
	}
	~Sentinel() override {
		beginContextShutdown("scene deletion");  // headless: the scene is deleted before the engine
	}
};

}  // namespace

int start(RackPyHostInit* args) {
	markMainThread();
	if (Py_IsInitialized()) {
		setError(args, "CPython is already initialized in this process. RackPy owns its interpreter and will not share one it did not create.");
		return 1;
	}
	if (!initializeInterpreter(args))
		return 2;
	INFO("RackPy interpreter started: Python %s", Py_GetVersion());
	setInterpreterAlive(true);
	bool ok = false;
	try {
		registerErrorTranslator();
		// Trusted plugin-shipped code only; patch files never run code (ADR 002, decision 9).
		py::module_::import("sys").attr("path").attr("insert")(0, args->python_path);
		py::module_::import("rackpy._bootstrap").attr("start")(args->plugin_dir, args->entry);
		ok = true;
	}
	catch (const std::exception& e) {
		setError(args, e.what());
	}
	// Plugin init() runs on the main thread; release the GIL until destroy().
	mainThreadState = PyEval_SaveThread();
	return ok ? 0 : 3;
}

std::shared_ptr<ContextToken> bindCurrentContext() {
	rack::Context* context = rack::contextGet();
	bool wasBound = binding().current() != nullptr;
	std::shared_ptr<ContextToken> token = binding().observe(context);
	if (token && !wasBound && context->scene)
		context->scene->addChild(new Sentinel);
	return token;
}

void beginContextShutdown(const char* source) {
	if (shutdownBegun || !binding().current())
		return;
	shutdownBegun = true;
	INFO("RackPy context shutdown begins (%s)", source);
	// Step 2: Python stops and joins its own threads while the context is still alive.
	callPython("context closing", [] { bootstrapHook("context_closing"); });
	// Step 3: in-flight engine calls from Python finish; later ones raise StaleHandleError.
	binding().close();
	INFO("RackPy context closed; handles to it are now stale");
}

void finalize() {
	if (!mainThreadState)
		return;
	binding().reset();  // step 5: every handle is stale from here on
	PyEval_RestoreThread(mainThreadState);
	mainThreadState = nullptr;
	bootstrapHook("stop");
	for (auto it = detachers.rbegin(); it != detachers.rend(); ++it) {
		try {
			(*it)();
		}
		catch (const std::exception& e) {
			WARN("RackPy: a native component failed to release its Python references: %s", e.what());
		}
	}
	detachers.clear();
	drainReleases();
	clearIdentities();
	dropErrorTypes();
	// Collect cycles so that wrappers which own native references release them now.
	try {
		py::module_::import("gc").attr("collect")();
	}
	catch (const std::exception&) {
	}
	drainReleases();
	size_t held = nativeReferences();
	size_t owned = transferredObjects();
	if (held || owned) {
		// A native object still owns Python objects, or Rack still owns objects whose destructors
		// need Python. Finalizing now would free them under it (ADR 003, decision 11), so leave the
		// interpreter running instead.
		std::string msg = rack::string::f("%zu Python reference(s) are still held by native objects and %zu "
			"Python-created object(s) are still owned by Rack", held, owned);
		diagnose(DiagnosticCode::ShutdownLeak, msg.c_str());
		WARN("RackPy did not finalize the interpreter: %s. It stays loaded until Rack exits.", msg.c_str());
		setInterpreterAlive(false);  // later releases leak instead of touching Python
		PyEval_SaveThread();
		outcome = "refused";
		return;
	}
	setInterpreterAlive(false);
	int rc = Py_FinalizeEx();
	outcome = "finalized";
	INFO("RackPy interpreter finalized: Py_FinalizeEx returned %d", rc);
}

void onFinalize(std::function<void()> detach) {
	detachers.push_back(std::move(detach));
}

std::string finalizeOutcome() {
	return outcome;
}

}  // namespace runtime
}  // namespace rackpy
