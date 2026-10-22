// SPDX-License-Identifier: GPL-3.0-or-later
// RackPy host library: owns the embedded CPython interpreter.
//
// Promoted from the RP-04 spike (spikes/embedding/src/host_core.cpp). The
// interpreter configuration is ADR 002, decision 5; start-up and shutdown
// follow decision 6 and ADR 003. This skeleton registers no Rack models and
// exposes no Rack API to Python yet: RP-10 adds context handles and the
// ownership registry, and the family tasks add bindings.
#include <pybind11/embed.h>

#include <rack.hpp>

#include <cstdio>
#include <string>

#include "rackpy/host_abi.h"

namespace py = pybind11;

namespace rackpy {
namespace host {

namespace {

/** Saved by PyEval_SaveThread() once start-up finishes; non-null while the interpreter is alive. */
PyThreadState* mainThreadState = nullptr;

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

/** Runs with the GIL held. Every py::object it creates is gone when it returns. */
bool start(RackPyHostInit* args) {
	try {
		// Trusted plugin-shipped code only; patch files never run code (ADR 002, decision 9).
		py::module_::import("sys").attr("path").attr("insert")(0, args->python_path);
		py::module_::import("rackpy._bootstrap").attr("start")(args->plugin_dir, args->entry);
		return true;
	}
	catch (const std::exception& e) {
		setError(args, e.what());
		return false;
	}
}

void stop() {
	try {
		py::module_::import("rackpy._bootstrap").attr("stop")();
	}
	catch (const std::exception& e) {
		WARN("RackPy shutdown hook raised: %s", e.what());
	}
}

}  // namespace

int init(RackPyHostInit* args) {
	if (args->abi != RACKPY_HOST_ABI) {
		setError(args, rack::string::f("loader ABI %d does not match host ABI %d", args->abi, RACKPY_HOST_ABI));
		return 1;
	}
	if (Py_IsInitialized()) {
		setError(args, "CPython is already initialized in this process. RackPy owns its interpreter and will not share one it did not create.");
		return 1;
	}
	if (!initializeInterpreter(args))
		return 2;
	INFO("RackPy interpreter started: Python %s", Py_GetVersion());
	bool ok = start(args);
	// Plugin init() runs on the main thread; release the GIL until destroy().
	mainThreadState = PyEval_SaveThread();
	return ok ? 0 : 3;
}

void destroy() {
	if (!mainThreadState)
		return;
	PyEval_RestoreThread(mainThreadState);
	mainThreadState = nullptr;
	stop();
	// Every py::object must be released before this point (ADR 003, decision 11).
	int rc = Py_FinalizeEx();
	INFO("RackPy interpreter finalized: Py_FinalizeEx returned %d", rc);
}

}  // namespace host
}  // namespace rackpy

extern "C" __attribute__((visibility("default"))) int rackpy_host_init(RackPyHostInit* args) {
	try {
		return rackpy::host::init(args);
	}
	catch (...) {
		if (args->error && args->error_size)
			std::snprintf(args->error, args->error_size, "unexpected C++ exception in host init");
		return 99;
	}
}

extern "C" __attribute__((visibility("default"))) void rackpy_host_destroy() {
	try {
		rackpy::host::destroy();
	}
	catch (...) {
		WARN("RackPy: unexpected C++ exception in host destroy");
	}
}
