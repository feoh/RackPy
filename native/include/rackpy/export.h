/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A function a RackPy library exports by name: the loader's, the host's and a kernel library's entry points.
   Everything else stays private: on Linux and macOS through -fvisibility=hidden, on Windows because a DLL
   with any __declspec(dllexport) symbol exports only those (RP-41). */
#pragma once

#if defined(_WIN32)
#define RACKPY_EXPORT __declspec(dllexport)
#else
#define RACKPY_EXPORT __attribute__((visibility("default")))
#endif
