// SPDX-License-Identifier: GPL-3.0-or-later
// RackPy host library: the C ABI the loader calls. The interpreter owner and
// the rest of the runtime live in native/runtime; this file only makes sure no
// C++ exception crosses into the loader (ADR 002, decision 1).
#include <pybind11/embed.h>

#include <rack.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "../bindings/plugin.hpp"
#include "../plugin/model.hpp"
#include "../dsp/runtime/runtime.hpp"
#include "rackpy/runtime/interpreter.hpp"
#include "rackpy/host_abi.h"

#ifdef RACKPY_TEST_HOOKS
#include "testing.hpp"
#endif

namespace {

/** rackpy-loader.json's "kernels": library paths relative to the plugin folder. */
std::vector<std::string> kernelLibraries(const std::string& pluginDir) {
	std::vector<std::string> out;
	json_error_t error;
	json_t* rootJ = json_load_file(rack::system::join(pluginDir, "rackpy-loader.json").c_str(), 0, &error);
	if (!rootJ)
		return out;  // the loader already read and checked it
	json_t* kernelsJ = json_object_get(rootJ, "kernels");
	if (kernelsJ && !json_is_array(kernelsJ))
		WARN("RackPy: rackpy-loader.json \"kernels\" must be a list of paths; ignoring it");
	size_t i;
	json_t* entryJ;
	json_array_foreach(kernelsJ, i, entryJ) {
		if (json_is_string(entryJ))
			out.push_back(json_string_value(entryJ));
		else
			WARN("RackPy: rackpy-loader.json \"kernels\" entry #%d is not a path; ignoring it", (int) i);
	}
	json_decref(rootJ);
	return out;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int rackpy_host_init(RackPyHostInit* args) {
	try {
		if (args->abi != RACKPY_HOST_ABI) {
			std::snprintf(args->error, args->error_size, "loader ABI %d does not match host ABI %d", args->abi, RACKPY_HOST_ABI);
			return 1;
		}
		auto* plugin = static_cast<rack::plugin::Plugin*>(args->plugin);
		// The entry module adds its models while it is imported (RP-16A).
		rackpy::plugin::setInstance(plugin);
		rackpy::plugin::openRegistration();
		// Kernel libraries first, so the entry module can use their types (RP-17B).
		rackpy::dsp::loadKernelLibraries(args->plugin_dir, kernelLibraries(args->plugin_dir));
		int rc = rackpy::runtime::start(args);
#ifdef RACKPY_TEST_HOOKS
		if (rc == 0)
			rackpy::testing::registerModels(plugin);
#endif
		if (rc != 0)
			rackpy::bindings::discardModels();
		else {
			// Rack rejects a plugin whose models differ from plugin.json, after init and
			// without calling destroy(): check first, and add none on a mismatch.
			std::string problem = rackpy::bindings::commitModels(plugin, rack::system::join(args->plugin_dir, "plugin.json"));
			if (!problem.empty()) {
				std::snprintf(args->error, args->error_size, "%s", problem.c_str());
				rc = 4;
			}
		}
		return rc;
	}
	catch (...) {
		if (args->error && args->error_size)
			std::snprintf(args->error, args->error_size, "unexpected C++ exception in host init");
		return 99;
	}
}

extern "C" __attribute__((visibility("default"))) void rackpy_host_destroy() {
	try {
		rackpy::runtime::finalize();
	}
	catch (...) {
		WARN("RackPy: unexpected C++ exception in host destroy");
	}
}

extern "C" __attribute__((visibility("default"))) void* rackpy_host_settings_to_json() {
	try {
		return rackpy::bindings::settingsToJson();
	}
	catch (...) {
		WARN("RackPy: unexpected C++ exception in settingsToJson");
		return nullptr;
	}
}

extern "C" __attribute__((visibility("default"))) void rackpy_host_settings_from_json(void* json) {
	try {
		rackpy::bindings::settingsFromJson(static_cast<json_t*>(json));
	}
	catch (...) {
		WARN("RackPy: unexpected C++ exception in settingsFromJson");
	}
}
