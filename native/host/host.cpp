// SPDX-License-Identifier: GPL-3.0-or-later
// RackPy host library: the C ABI the loader calls. The interpreter owner and
// the rest of the runtime live in native/runtime; this file only makes sure no
// C++ exception crosses into the loader (ADR 002, decision 1).
#include <pybind11/embed.h>

#include <rack.hpp>

#include <cstdio>
#include <string>

#include "../bindings/plugin.hpp"
#include "../plugin/model.hpp"
#include "rackpy/runtime/interpreter.hpp"
#include "rackpy/host_abi.h"

#ifdef RACKPY_TEST_HOOKS
#include "testing.hpp"
#endif

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
