// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only loader for the dynamic audio-path guard API.
#pragma once

#include <cstddef>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdlib>
#else
#include <dlfcn.h>
#endif

namespace rackpy::testing {

using GuardAction = void (*)();
using GuardReport = void (*)(char*, std::size_t);
using GuardInstall = int (*)();

struct AudioGuardApi {
	GuardAction enter = nullptr;
	GuardAction leave = nullptr;
	GuardAction reset = nullptr;
	GuardReport report = nullptr;
#if defined(_WIN32)
	HMODULE module = nullptr;
#endif

	bool load() {
#if defined(_WIN32)
		const char* path = std::getenv("RACKPY_AUDIO_GUARD_DLL");
		if (!path || !*path)
			return false;
		module = LoadLibraryA(path);
		if (!module)
			return false;
		enter = reinterpret_cast<GuardAction>(GetProcAddress(module, "audio_guard_enter"));
		leave = reinterpret_cast<GuardAction>(GetProcAddress(module, "audio_guard_leave"));
		reset = reinterpret_cast<GuardAction>(GetProcAddress(module, "audio_guard_reset"));
		report = reinterpret_cast<GuardReport>(GetProcAddress(module, "audio_guard_report"));
		auto install = reinterpret_cast<GuardInstall>(GetProcAddress(module, "audio_guard_install"));
		return enter && leave && reset && report && install && install() != 0;
#else
		enter = reinterpret_cast<GuardAction>(dlsym(RTLD_DEFAULT, "audio_guard_enter"));
		leave = reinterpret_cast<GuardAction>(dlsym(RTLD_DEFAULT, "audio_guard_leave"));
		reset = reinterpret_cast<GuardAction>(dlsym(RTLD_DEFAULT, "audio_guard_reset"));
		report = reinterpret_cast<GuardReport>(dlsym(RTLD_DEFAULT, "audio_guard_report"));
		return enter && leave && reset && report;
#endif
	}
};

}  // namespace rackpy::testing
