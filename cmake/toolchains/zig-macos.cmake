# SPDX-License-Identifier: GPL-3.0-or-later
# Cross-compiles the macOS plugin on Linux with Zig, to check that it compiles and links (RP-40).
# A check only: Zig links its own static libc++, where Rack and a real macOS build use the system's
# libc++.dylib, so C++ objects crossing into Rack would not match. Never run such a build in Rack or
# distribute it; macOS artifacts are built on macOS (bin/rackpy-dev build on a Mac).
#
#   bin/rackpy-dev build --platform mac-arm64 --toolchain zig-macos --build-dir build/zig-mac-arm64
set(CMAKE_SYSTEM_NAME Darwin)
if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
  set(CMAKE_SYSTEM_PROCESSOR arm64)
  set(_rackpy_zig_target aarch64-macos.11.0)
elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
  set(CMAKE_SYSTEM_PROCESSOR x86_64)
  set(_rackpy_zig_target x86_64-macos.10.15)
else()
  message(FATAL_ERROR "zig-macos needs -DCMAKE_OSX_ARCHITECTURES=arm64 or x86_64")
endif()
find_program(_rackpy_zig zig REQUIRED)
# CMake wants one executable per compiler; these wrappers pass Zig's subcommand and target. They drop
# -march: Zig names CPUs (apple_m1), not Clang's armv8-a+fp+simd, and the target's baseline is enough
# for a compile check.
foreach(_lang cc c++)
  set(_wrapper "${CMAKE_BINARY_DIR}/zig-${_lang}")
  file(WRITE "${_wrapper}" "#!/bin/sh\nfor a do shift; case \"$a\" in -march=*) ;; *) set -- \"$@\" \"$a\";; esac; done\nexec \"${_rackpy_zig}\" ${_lang} -target ${_rackpy_zig_target} \"$@\"\n")
  file(CHMOD "${_wrapper}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
endforeach()
set(CMAKE_C_COMPILER "${CMAKE_BINARY_DIR}/zig-cc")
set(CMAKE_CXX_COMPILER "${CMAKE_BINARY_DIR}/zig-c++")
# Zig has no install_name_tool; LLVM's works on any host.
find_program(RACKPY_INSTALL_NAME_TOOL NAMES llvm-install-name-tool install_name_tool REQUIRED)
set(CMAKE_INSTALL_NAME_TOOL "${RACKPY_INSTALL_NAME_TOOL}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
