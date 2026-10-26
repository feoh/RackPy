// SPDX-License-Identifier: GPL-3.0-or-later
// Promoted from the RP-04 spike host (ADR 002, decisions 5 and 6) and the RP-06
// lifecycle probe's teardown sentinel (ADR 003, decision 13).
#include "rackpy/runtime/interpreter.hpp"

#include <cstdio>
#include <limits>
#include <memory>
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
std::vector<std::function<void()>> uiCleanups, contextCleanups;
std::vector<std::function<void()>> graphicsDestroyHooks, graphicsCreateHooks;
bool sentinelInstalled = false;

void runCleanups(const std::vector<std::function<void()>>& hooks) noexcept {
	for (const auto& hook : hooks) {
		try { hook(); }
		catch (const std::exception& e) { WARN("RackPy UI cleanup failed: %s", e.what()); }
		catch (...) { WARN("RackPy UI cleanup failed"); }
	}
}

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

/** First in the child list, so last in reverse graphics-event delivery and
first in scene destruction. This is deliberately separate from the last-step
sentinel: Python cleanup must not run before the other widgets' graphics events. */
struct GraphicsAnchor : rack::widget::Widget {
	void onContextCreate(const ContextCreateEvent& e) override {
		Widget::onContextCreate(e);
		if (!shutdownBegun)
			runCleanups(graphicsCreateHooks);
	}
	void onContextDestroy(const ContextDestroyEvent& e) override {
		Widget::onContextDestroy(e);
		if (!shutdownBegun)
			runCleanups(graphicsDestroyHooks);
		// The Context shutdown protocol waits for scene deletion (below), even when the window
		// closes: Rack saves its autosave after deleting the window, and the patch needs the Python
		// module widgets (positions, cables) and the modules they own.
	}
	~GraphicsAnchor() override {
		if (sentinelInstalled)
			beginContextShutdown("scene deletion");
	}
};

/** Last-step maintenance, without owning the graphics lifetime boundary. */
struct Sentinel : rack::widget::Widget {
	void step() override {
		Widget::step();
		if (!shutdownBegun)
			runCleanups(uiCleanups);
		// Python references dropped on engine threads are released here, on the UI thread.
		if (pendingReleases())
			callPython("deferred releases", [] {});
	}
	~Sentinel() override {
		if (sentinelInstalled)
			beginContextShutdown("scene deletion fallback");
		sentinelInstalled = false;
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
	std::shared_ptr<ContextToken> token = binding().observe(context);
	if (token && !sentinelInstalled && context->scene) {
		auto anchor = std::make_unique<GraphicsAnchor>();
		auto sentinel = std::make_unique<Sentinel>();
		const float inf = std::numeric_limits<float>::infinity();
		anchor->box.size = sentinel->box.size = rack::math::Vec(inf, inf);
		try {
			context->scene->addChildBottom(anchor.get());
			context->scene->addChild(sentinel.get());
		}
		catch (...) {
			// Allocation failure can follow Widget's parent assignment. Both
			// nodes have no children/overrides and need no EventState callbacks.
			context->scene->children.remove(anchor.get());
			context->scene->children.remove(sentinel.get());
			anchor->parent = sentinel->parent = nullptr;
			throw;
		}
		anchor.release();
		sentinel.release();
		sentinelInstalled = true;
	}
	return token;
}

void beginContextShutdown(const char* source) {
	if (shutdownBegun || !binding().current())
		return;
	shutdownBegun = true;
	INFO("RackPy context shutdown begins (%s)", source);
	// Step 2: Python stops and joins its own threads while the context is still alive.
	callPython("context closing", [] { bootstrapHook("context_closing"); });
	// Detached UI objects must die while the context's EventState still exists.
	runCleanups(contextCleanups);
	contextCleanups.clear();
	uiCleanups.clear();
	graphicsDestroyHooks.clear();
	graphicsCreateHooks.clear();
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

void onUiStep(std::function<void()> action) {
	requireMainThread("registering UI cleanup");
	uiCleanups.push_back(std::move(action));
}
void onContextClosing(std::function<void()> action) {
	requireMainThread("registering context cleanup");
	contextCleanups.push_back(std::move(action));
}
void onGraphicsContextDestroy(std::function<void()> action) {
	requireMainThread("registering graphics cleanup");
	graphicsDestroyHooks.push_back(std::move(action));
}
void onGraphicsContextCreate(std::function<void()> action) {
	requireMainThread("registering graphics creation");
	graphicsCreateHooks.push_back(std::move(action));
}

std::string finalizeOutcome() {
	return outcome;
}

}  // namespace runtime
}  // namespace rackpy
