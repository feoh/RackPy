// SPDX-License-Identifier: GPL-3.0-or-later
// C ABI between the thin loader (plugin.so) and a host library.
// Only C types cross it: each DSO links libstdc++ statically, so C++
// exceptions and RTTI must never cross this boundary.
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RACKPY_SPIKE_HOST_ABI 1

typedef struct RackPySpikeHostInit {
	int abi;
	void* plugin;  // rack::plugin::Plugin*
	const char* plugin_dir;
	const char* python_home;
	const char* script;  // trusted script shipped inside the plugin directory
	char* error;  // out: NUL-terminated message on failure
	size_t error_size;
} RackPySpikeHostInit;

typedef int (*RackPySpikeHostInitFn)(RackPySpikeHostInit* args);
typedef void (*RackPySpikeHostDestroyFn)(void);

#ifdef __cplusplus
}
#endif
