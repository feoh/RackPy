// SPDX-License-Identifier: GPL-3.0-or-later
// Rack module, widget and interpreter lifecycle shared by both binding hosts.
#include "host_core.hpp"

#include <cstdio>
#include <cstring>
#include <thread>

namespace spike {

namespace {

PyThreadState* mainThreadState = nullptr;
std::thread::id mainThread;
thread_local rack::widget::Widget* configuringParent = nullptr;

std::string threadName() {
	return std::this_thread::get_id() == mainThread ? "main" : "other";
}

void setError(RackPySpikeHostInit* args, const std::string& msg) {
	if (args->error && args->error_size)
		std::snprintf(args->error, args->error_size, "%s", msg.c_str());
}

struct SpikeModule : rack::engine::Module {
	enum ParamId { GAIN_PARAM, NUM_PARAMS };
	enum InputId { IN_INPUT, NUM_INPUTS };
	enum OutputId { OUT_OUTPUT, NUM_OUTPUTS };

	std::shared_ptr<Token> token = std::make_shared<Token>();

	SpikeModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(GAIN_PARAM, 0.f, 2.f, 1.f, "Gain");
		configInput(IN_INPUT, "In");
		configOutput(OUT_OUTPUT, "Out");
		INFO("RACKPY_SPIKE module constructed thread=%s", threadName().c_str());
	}

	~SpikeModule() override {
		// Native only: invalidate borrowed Python handles without touching Python.
		token->alive.store(false);
		INFO("RACKPY_SPIKE module deleted id=%lld processed=%llu thread=%s", (long long) token->moduleId.load(),
			(unsigned long long) token->processed.load(), threadName().c_str());
	}

	void onAdd(const AddEvent& e) override {
		token->moduleId.store(id);
	}

	// Production path: native, no Python, no allocation, no locks.
	void process(const ProcessArgs& args) override {
		float g = token->gain.load(std::memory_order_relaxed) * params[GAIN_PARAM].getValue();
		float out = inputs[IN_INPUT].getNormalVoltage(1.f) * g;
		outputs[OUT_OUTPUT].setVoltage(out);
		token->lastOut.store(out, std::memory_order_relaxed);
		token->processed.fetch_add(1, std::memory_order_relaxed);
	}
};

struct SpikeWidget : rack::app::ModuleWidget {
	SpikeWidget(SpikeModule* module) {
		setModule(module);
		box.size = rack::math::Vec(rack::app::RACK_GRID_WIDTH * 4, rack::app::RACK_GRID_HEIGHT);
		// Module is NULL in browser previews and screenshots; never call Python for it.
		if (!module || !mainThreadState)
			return;
		INFO("RACKPY_SPIKE widget callback thread=%s context=%s", threadName().c_str(), rack::contextGet() ? "set" : "null");
		PyGILState_STATE gil = PyGILState_Ensure();
		configuringParent = this;
		bindingOps().onWidgetCreated(Handle{module->token});
		configuringParent = nullptr;
		PyGILState_Release(gil);
	}
};

}  // namespace

rack::widget::Widget* currentParent() {
	return configuringParent;
}

int hostInit(RackPySpikeHostInit* args) {
	mainThread = std::this_thread::get_id();
	// Standalone Rack calls plugin init() before it creates the Context.
	INFO("RACKPY_SPIKE host init context=%s", rack::contextGet() ? "set" : "null");
	const BindingOps& ops = bindingOps();
	if (Py_IsInitialized()) {
		setError(args, "CPython is already initialized in this process. RackPy owns its interpreter and will not share one it did not create.");
		return 1;
	}
	if (!ops.registerInittab()) {
		setError(args, "PyImport_AppendInittab failed");
		return 1;
	}

	PyPreConfig preconfig;
	PyPreConfig_InitIsolatedConfig(&preconfig);
	preconfig.utf8_mode = 1;
	PyStatus status = Py_PreInitialize(&preconfig);
	if (PyStatus_Exception(status)) {
		setError(args, std::string("Py_PreInitialize: ") + (status.err_msg ? status.err_msg : "unknown error"));
		return 2;
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
	std::string program = std::string(args->plugin_dir) + "/rackpy-spike";
	const char* failure = nullptr;
	status = PyConfig_SetBytesString(&config, &config.home, args->python_home);
	if (PyStatus_Exception(status))
		failure = "home";
	if (!failure) {
		status = PyConfig_SetBytesString(&config, &config.program_name, program.c_str());
		if (PyStatus_Exception(status))
			failure = "program_name";
	}
	if (!failure) {
		status = Py_InitializeFromConfig(&config);
		if (PyStatus_Exception(status))
			failure = "Py_InitializeFromConfig";
	}
	PyConfig_Clear(&config);
	if (failure) {
		// Never Py_ExitStatusException(): it would exit Rack.
		setError(args, std::string(failure) + ": " + (status.err_msg ? status.err_msg : "unknown error"));
		return 2;
	}
	INFO("RACKPY_SPIKE interpreter initialized binding=%s python=%s", ops.name, Py_GetVersion());

	// Trusted plugin-shipped code only. Patch files are never executed.
	PyObject* sysPath = PySys_GetObject("path");  // borrowed
	PyObject* dir = PyUnicode_FromString(args->plugin_dir);
	if (!sysPath || !dir || PyList_Insert(sysPath, 0, dir) != 0) {
		Py_XDECREF(dir);
		PyErr_Clear();
		setError(args, "could not add the plugin directory to sys.path");
		mainThreadState = PyEval_SaveThread();
		return 3;
	}
	Py_DECREF(dir);
	std::string err;
	bool ok = ops.afterInit(args->script, err);
	mainThreadState = PyEval_SaveThread();
	if (!ok) {
		setError(args, "script failed: " + err);
		return 3;
	}

	auto* plugin = static_cast<rack::plugin::Plugin*>(args->plugin);
	plugin->addModel(rack::createModel<SpikeModule, SpikeWidget>("SpikeModule"));
	return 0;
}

void hostDestroy() {
	if (!mainThreadState)
		return;
	INFO("RACKPY_SPIKE destroy thread=%s", threadName().c_str());
	PyEval_RestoreThread(mainThreadState);
	mainThreadState = nullptr;
	bindingOps().beforeFinalize();
	int rc = Py_FinalizeEx();
	INFO("RACKPY_SPIKE Py_FinalizeEx rc=%d", rc);
}

}  // namespace spike

extern "C" __attribute__((visibility("default"))) int rackpy_spike_host_init(RackPySpikeHostInit* args) {
	try {
		return spike::hostInit(args);
	}
	catch (...) {
		if (args->error && args->error_size)
			std::snprintf(args->error, args->error_size, "unexpected C++ exception in host init");
		return 99;
	}
}

extern "C" __attribute__((visibility("default"))) void rackpy_spike_host_destroy() {
	try {
		spike::hostDestroy();
	}
	catch (...) {
		WARN("RACKPY_SPIKE unexpected C++ exception in host destroy");
	}
}
