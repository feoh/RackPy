// SPDX-License-Identifier: GPL-3.0-or-later
// Loading the libraries a plugin ships, the same way on every platform (RP-41).
//
// The implementation (native/runtime/dynlib.cpp) is the only RackPy file that
// includes <dlfcn.h> or <windows.h>; Rack translation units include this header
// instead, so Windows' macros never meet Rack's headers.
#pragma once

#include <string>
#include <vector>

namespace rackpy {
namespace dynlib {

/** Loads the library at a full UTF-8 path and keeps it loaded for the life of the process (RTLD_NODELETE;
on Windows a pinned module), so code and static objects it registered never disappear. global makes its
symbols resolve later libraries' undefined references (RTLD_GLOBAL; Windows resolves each DLL's imports by
name, so it has no effect there). On Windows the library's own dependencies are searched for only in its
folder and in System32, never on PATH or in the current directory; a DLL of the same name that is already
loaded is used instead, as Windows always does. Returns nullptr and sets error on failure. */
void* openPinned(const std::string& path, bool global, std::string& error);

/** An exported function or object of a library openPinned returned, or nullptr. */
void* symbol(void* handle, const char* name);

/** The file of every image (executable or library) loaded in this process, as the system reports it. */
std::vector<std::string> loadedImages();

/** PATH with every symlink (on Windows also junction) resolved, as the system itself resolves it, in UTF-8 with
'/' separators; "" if it cannot be resolved (it does not exist, or the system refuses). RackPy's containment
checks use it instead of rack::system::getCanonical, whose std::filesystem::canonical can throw on Windows
(MinGW's libstdc++: std::logic_error for a Wine Z: path, RP-41), which that function does not catch. */
std::string canonicalPath(const std::string& path);

}  // namespace dynlib
}  // namespace rackpy
