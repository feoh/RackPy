// SPDX-License-Identifier: GPL-3.0-or-later
// RackPy plugin entry point (plugin.so). It has no Python dependency, so a
// missing or wrong runtime produces a log message instead of a dlopen
// failure, and nothing can throw across init() or destroy().
//
// Promoted from the RP-04 spike loader (spikes/embedding/src/loader.cpp); the
// loading order is ADR 002, decision 1.
#include <rack.hpp>

#include <dlfcn.h>

#include "loader_config.hpp"
#include "rackpy/host_abi.h"

using namespace rack;

Plugin* pluginInstance;

namespace {

void* hostHandle = nullptr;
RackPyHostSettingsToJsonFn hostSettingsToJson = nullptr;
RackPyHostSettingsFromJsonFn hostSettingsFromJson = nullptr;

void refuse(const std::string& why) {
	WARN("RackPy refused to start: %s", why.c_str());
}

void loadHost(Plugin* p) {
	using namespace rackpy::loader;
	std::string dir = asset::plugin(p, "");
	std::string runtime = asset::plugin(p, "python");
	Config cfg;
	std::string err;
	if (!readConfig(asset::plugin(p, "rackpy-loader.json"), cfg, err))
		return refuse(err);

	// One CPython owner per process (ADR 002, decision 4). dl_iterate_phdr also
	// sees libraries other plugins opened with RTLD_LOCAL.
	std::vector<std::string> pythons = loadedPythons();
	if (!pythons.empty()) {
		return refuse(string::f("a CPython runtime is already loaded in this process (%s). "
			"RackPy must own the only interpreter; remove the other Python-embedding plugin or preload.",
			pythons[0].c_str()));
	}

	std::string libpython = system::join(runtime, cfg.libpython);
	std::string marker = system::join(runtime, cfg.stdlibMarker);
	if (!system::isFile(libpython) || !system::isFile(marker)) {
		return refuse(string::f("bundled Python runtime missing or incomplete (expected %s and %s). "
			"Reinstall the plugin package; RackPy never falls back to a system Python.",
			libpython.c_str(), marker.c_str()));
	}

	// Extension modules in the bundled runtime resolve Py* symbols globally.
	if (!dlopen(libpython.c_str(), RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE))
		return refuse(string::f("cannot load %s: %s", libpython.c_str(), dlerror()));
	std::string hostPath = system::join(dir, cfg.host);
	void* handle = dlopen(hostPath.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
	if (!handle)
		return refuse(string::f("cannot load host %s: %s", hostPath.c_str(), dlerror()));
	auto hostInit = (RackPyHostInitFn) dlsym(handle, RACKPY_HOST_INIT_SYMBOL);
	auto hostDestroy = (RackPyHostDestroyFn) dlsym(handle, RACKPY_HOST_DESTROY_SYMBOL);
	if (!hostInit || !hostDestroy)
		return refuse(hostPath + " does not export the RackPy host ABI");

	std::string pythonPath = system::join(dir, cfg.pythonPath);
	char error[2048] = "";
	RackPyHostInit args = {
		RACKPY_HOST_ABI, p, dir.c_str(), runtime.c_str(), pythonPath.c_str(), cfg.entry.c_str(), error, sizeof(error)};
	int rc = hostInit(&args);
	if (rc != 0) {
		WARN("RackPy host initialization failed (%d): %s", rc, error);
		// Rack never calls destroy() for a plugin whose init() registered nothing
		// it expected, so tear down whatever the host started now.
		hostDestroy();
		return;
	}
	hostHandle = handle;
	hostSettingsToJson = (RackPyHostSettingsToJsonFn) dlsym(handle, RACKPY_HOST_SETTINGS_TO_JSON_SYMBOL);
	hostSettingsFromJson = (RackPyHostSettingsFromJsonFn) dlsym(handle, RACKPY_HOST_SETTINGS_FROM_JSON_SYMBOL);
	INFO("RackPy host %s ready", cfg.host.c_str());
}

}  // namespace

extern "C" void init(Plugin* p) {
	pluginInstance = p;
	try {
		loadHost(p);
	}
	catch (Exception& e) {
		refuse(e.what());
	}
	catch (...) {
		refuse("unexpected C++ exception in the loader");
	}
}

extern "C" void destroy() {
	if (!hostHandle)
		return;
	auto hostDestroy = (RackPyHostDestroyFn) dlsym(hostHandle, RACKPY_HOST_DESTROY_SYMBOL);
	hostHandle = nullptr;
	hostSettingsToJson = nullptr;
	hostSettingsFromJson = nullptr;
	if (hostDestroy)
		hostDestroy();
	// RTLD_NODELETE keeps the host and libpython mapped after Rack dlcloses this plugin.
}

// Plugin-wide settings (RP-15D). Rack stores what settingsToJson returns in
// settings.json; NULL leaves the stored value as it is. Without a running
// host there is nothing to save or load.
extern "C" json_t* settingsToJson() {
	return hostSettingsToJson ? static_cast<json_t*>(hostSettingsToJson()) : nullptr;
}

extern "C" void settingsFromJson(json_t* rootJ) {
	if (hostSettingsFromJson)
		hostSettingsFromJson(rootJ);
}
