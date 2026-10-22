// SPDX-License-Identifier: GPL-3.0-or-later
// Naive variant for comparison: plugin.so links libpython and the pybind11
// host directly, so Rack's dlopen(RTLD_LOCAL) also loads libpython locally.
#include "host_core.hpp"

rack::plugin::Plugin* pluginInstance;

extern "C" __attribute__((visibility("default"))) void init(rack::plugin::Plugin* p) {
	pluginInstance = p;
	std::string dir = rack::asset::plugin(p, "");
	std::string runtime = rack::asset::plugin(p, "python");
	char error[1024] = "";
	RackPySpikeHostInit args = {RACKPY_SPIKE_HOST_ABI, p, dir.c_str(), runtime.c_str(), "spike_plugin", error, sizeof(error)};
	int rc = rackpy_spike_host_init(&args);
	if (rc != 0)
		WARN("RACKPY_SPIKE_DIRECT host init failed (%d): %s", rc, error);
	else
		INFO("RACKPY_SPIKE_DIRECT ready");
}

extern "C" __attribute__((visibility("default"))) void destroy() {
	rackpy_spike_host_destroy();
}
