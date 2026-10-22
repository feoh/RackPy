# SPDX-License-Identifier: GPL-3.0-or-later
# The official Rack plugin toolchain for Linux x64 (crosstool-ng
# x86_64-ubuntu16.04-linux-gnu, glibc 2.23), built by bin/rack-toolchain.
# Use it for builds meant to run on other machines; see docs/compatibility.md.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(RACK_TOOLCHAIN_BIN "$ENV{RACK_TOOLCHAIN_BIN}" CACHE PATH "bin/ directory of the toolchain (print it with: bin/rack-toolchain path)")
if(NOT RACK_TOOLCHAIN_BIN OR NOT EXISTS "${RACK_TOOLCHAIN_BIN}/x86_64-ubuntu16.04-linux-gnu-g++")
  message(FATAL_ERROR "RACK_TOOLCHAIN_BIN='${RACK_TOOLCHAIN_BIN}' does not contain x86_64-ubuntu16.04-linux-gnu-g++. Build it with `bin/rack-toolchain build`.")
endif()
# try_compile projects read this file again without the cache; pass the location on.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES RACK_TOOLCHAIN_BIN)
set(_prefix "${RACK_TOOLCHAIN_BIN}/x86_64-ubuntu16.04-linux-gnu-")
set(CMAKE_C_COMPILER "${_prefix}gcc")
set(CMAKE_CXX_COMPILER "${_prefix}g++")
set(CMAKE_AR "${_prefix}ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${_prefix}ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP "${_prefix}strip" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${_prefix}objcopy" CACHE FILEPATH "")
# The compilers carry their own sysroot. pybind11 is found through pybind11_DIR
# on the build machine, so CMake's default search modes are kept.
