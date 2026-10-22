// SPDX-License-Identifier: GPL-3.0-or-later
// C ABI between the RackPy loader (plugin.so) and the RackPy host library.
// Only C types cross it: each DSO links libstdc++ statically, so C++
// exceptions and RTTI must never cross this boundary (ADR 002, decision 1).
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bumped whenever RackPyHostInit or the exported functions change. */
#define RACKPY_HOST_ABI 1

typedef struct RackPyHostInit {
	int abi;  // RACKPY_HOST_ABI as the loader was built
	void* plugin;  // rack::plugin::Plugin*
	const char* plugin_dir;
	const char* python_home;  // bundled runtime; never a system Python
	const char* python_path;  // directory holding the rackpy package and trusted plugin code
	const char* entry;  // trusted module to import after rackpy starts, or "" for none
	char* error;  // out: NUL-terminated message on failure
	size_t error_size;
} RackPyHostInit;

/** Returns 0 on success. On failure fills `error`, and the loader calls rackpy_host_destroy(). */
typedef int (*RackPyHostInitFn)(RackPyHostInit* args);
typedef void (*RackPyHostDestroyFn)(void);

#define RACKPY_HOST_INIT_SYMBOL "rackpy_host_init"
#define RACKPY_HOST_DESTROY_SYMBOL "rackpy_host_destroy"

#ifdef __cplusplus
}
#endif
