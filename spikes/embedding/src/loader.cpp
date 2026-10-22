// SPDX-License-Identifier: GPL-3.0-or-later
// Thin Rack plugin entry point for the RP-04 spike. It has no Python
// dependency, so a missing or wrong runtime produces a log message instead of
// a dlopen failure, and nothing can throw across init().
#include <rack.hpp>

#include <dlfcn.h>
#include <link.h>

#include <string>
#include <vector>

#include "host_api.h"

using namespace rack;

Plugin* pluginInstance;

namespace {

void* hostHandle = nullptr;

struct LoaderConfig {
	std::string host;  // host library file name inside the plugin directory
	std::string libpython;  // relative to the runtime directory
	std::string stdlibMarker;  // a file that must exist in the runtime
	bool global = true;  // promote libpython to RTLD_GLOBAL
};

int collectPython(struct dl_phdr_info* info, size_t, void* data) {
	std::string name = info->dlpi_name ? info->dlpi_name : "";
	if (name.find("libpython") != std::string::npos)
		static_cast<std::vector<std::string>*>(data)->push_back(name);
	return 0;
}

std::vector<std::string> loadedPythons() {
	std::vector<std::string> names;
	dl_iterate_phdr(collectPython, &names);
	return names;
}

bool readConfig(const std::string& path, LoaderConfig& cfg, std::string& err) {
	json_error_t error;
	json_t* rootJ = json_load_file(path.c_str(), 0, &error);
	if (!rootJ) {
		err = string::f("cannot read %s: %s", path.c_str(), error.text);
		return false;
	}
	DEFER({json_decref(rootJ);});
	auto get = [&](const char* key, std::string& out) {
		json_t* j = json_object_get(rootJ, key);
		if (json_is_string(j))
			out = json_string_value(j);
		return !out.empty();
	};
	if (!get("host", cfg.host) || !get("libpython", cfg.libpython) || !get("stdlib_marker", cfg.stdlibMarker)) {
		err = path + " needs string fields host, libpython and stdlib_marker";
		return false;
	}
	json_t* globalJ = json_object_get(rootJ, "libpython_global");
	if (globalJ)
		cfg.global = json_is_true(globalJ);
	return true;
}

void loadHost(Plugin* p) {
	std::string dir = asset::plugin(p, "");
	std::string runtime = asset::plugin(p, "python");
	LoaderConfig cfg;
	std::string err;
	if (!readConfig(asset::plugin(p, "spike.json"), cfg, err)) {
		WARN("RACKPY_SPIKE_LOADER refused: %s", err.c_str());
		return;
	}

	// One CPython owner per process. dl_iterate_phdr also sees libraries other
	// plugins opened with RTLD_LOCAL, which a global symbol lookup would miss.
	std::vector<std::string> pythons = loadedPythons();
	if (!pythons.empty()) {
		WARN("RACKPY_SPIKE_LOADER refused: a CPython runtime is already loaded in this process (%s). "
			"RackPy needs to own the only interpreter; remove the other Python-embedding plugin or preload.",
			pythons[0].c_str());
		return;
	}

	std::string libpython = system::join(runtime, cfg.libpython);
	std::string marker = system::join(runtime, cfg.stdlibMarker);
	if (!system::isFile(libpython) || !system::isFile(marker)) {
		WARN("RACKPY_SPIKE_LOADER refused: bundled Python runtime missing or incomplete (expected %s and %s). "
			"Reinstall the plugin package; RackPy never falls back to a system Python.",
			libpython.c_str(), marker.c_str());
		return;
	}

	int mode = RTLD_NOW | RTLD_NODELETE | (cfg.global ? RTLD_GLOBAL : RTLD_LOCAL);
	if (!dlopen(libpython.c_str(), mode)) {
		WARN("RACKPY_SPIKE_LOADER refused: cannot load %s: %s", libpython.c_str(), dlerror());
		return;
	}
	std::string hostPath = system::join(dir, cfg.host);
	hostHandle = dlopen(hostPath.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
	if (!hostHandle) {
		WARN("RACKPY_SPIKE_LOADER refused: cannot load host %s: %s", hostPath.c_str(), dlerror());
		return;
	}
	auto hostInit = (RackPySpikeHostInitFn) dlsym(hostHandle, "rackpy_spike_host_init");
	if (!hostInit) {
		WARN("RACKPY_SPIKE_LOADER refused: %s has no rackpy_spike_host_init", hostPath.c_str());
		hostHandle = nullptr;
		return;
	}

	char error[1024] = "";
	RackPySpikeHostInit args = {RACKPY_SPIKE_HOST_ABI, p, dir.c_str(), runtime.c_str(), "spike_plugin", error, sizeof(error)};
	int rc = hostInit(&args);
	if (rc != 0) {
		WARN("RACKPY_SPIKE_LOADER host init failed (%d): %s", rc, error);
		// Rack rejects a plugin that registered no models and never calls
		// destroy(), so tear down whatever the host initialized now.
		auto hostDestroy = (RackPySpikeHostDestroyFn) dlsym(hostHandle, "rackpy_spike_host_destroy");
		if (hostDestroy)
			hostDestroy();
		hostHandle = nullptr;
		return;
	}
	INFO("RACKPY_SPIKE_LOADER host %s ready (libpython %s)", cfg.host.c_str(), cfg.global ? "global" : "local");
}

}  // namespace

extern "C" void init(Plugin* p) {
	pluginInstance = p;
	try {
		loadHost(p);
	}
	catch (Exception& e) {
		WARN("RACKPY_SPIKE_LOADER refused: %s", e.what());
	}
	catch (...) {
		WARN("RACKPY_SPIKE_LOADER refused: unexpected C++ exception");
	}
}

extern "C" void destroy() {
	if (!hostHandle)
		return;
	auto hostDestroy = (RackPySpikeHostDestroyFn) dlsym(hostHandle, "rackpy_spike_host_destroy");
	if (hostDestroy)
		hostDestroy();
	// RTLD_NODELETE keeps the host and libpython mapped after Rack dlcloses this plugin.
}
