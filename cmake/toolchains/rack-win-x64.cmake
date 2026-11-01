# SPDX-License-Identifier: GPL-3.0-or-later
# The official Rack plugin toolchain for Windows x64 (crosstool-ng x86_64-w64-mingw32, mingw-w64 v10,
# msvcrt), built by bin/rack-toolchain build --target win-x64: the compiler and C runtime Rack for
# Windows itself uses (RP-41). Cross-builds on Linux; the result runs only on Windows, so no tests here.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(RACK_TOOLCHAIN_BIN "$ENV{RACK_TOOLCHAIN_BIN}" CACHE PATH "bin/ directory of the toolchain (print it with: bin/rack-toolchain path --target win-x64)")
if(NOT RACK_TOOLCHAIN_BIN OR NOT EXISTS "${RACK_TOOLCHAIN_BIN}/x86_64-w64-mingw32-g++")
  message(FATAL_ERROR "RACK_TOOLCHAIN_BIN='${RACK_TOOLCHAIN_BIN}' does not contain x86_64-w64-mingw32-g++. Build it with `bin/rack-toolchain build --target win-x64`.")
endif()
# try_compile projects read this file again without the cache; pass the location on.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES RACK_TOOLCHAIN_BIN)
set(_prefix "${RACK_TOOLCHAIN_BIN}/x86_64-w64-mingw32-")
set(CMAKE_C_COMPILER "${_prefix}gcc")
set(CMAKE_CXX_COMPILER "${_prefix}g++")
set(CMAKE_RC_COMPILER "${_prefix}windres")
set(CMAKE_AR "${_prefix}ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${_prefix}ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP "${_prefix}strip" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${_prefix}objcopy" CACHE FILEPATH "")
