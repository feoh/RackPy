# SPDX-License-Identifier: GPL-3.0-or-later
# The pinned Rack SDK and the flags its compile.mk/plugin.mk give Linux x64 plugins.
#
# Defines:
#   rackpy_sdk_compile  compile flags and SDK headers (as -isystem, see ADR 004)
#   rackpy_sdk_plugin   compile + link flags for a DSO that Rack loads
#   rackpy_sdk_program  compile + link flags for a native test program outside Rack

set(RACK_SDK_DIR "" CACHE PATH "Extracted, verified Rack SDK for lin-x64 (print it with: bin/rack-sdk path)")

set(_rackpy_sdk_help "Fetch and verify the pinned SDK with `bin/rack-sdk path`, then configure with -DRACK_SDK_DIR=<that path>, or let `bin/rackpy-dev` do both.")
if(NOT RACK_SDK_DIR)
  message(FATAL_ERROR "RACK_SDK_DIR is not set. ${_rackpy_sdk_help}")
endif()
if(NOT EXISTS "${RACK_SDK_DIR}/include/rack.hpp" OR NOT EXISTS "${RACK_SDK_DIR}/libRack.so")
  message(FATAL_ERROR "RACK_SDK_DIR=${RACK_SDK_DIR} is not an extracted lin-x64 Rack SDK (no include/rack.hpp or libRack.so). ${_rackpy_sdk_help}")
endif()

# The SDK must be the pinned one: check libRack and every header reachable
# from rack.hpp against tests/fixtures/sdk-provenance.json (RP-02).
file(READ "${PROJECT_SOURCE_DIR}/tests/fixtures/sdk-provenance.json" _rackpy_provenance)
string(JSON _expected GET "${_rackpy_provenance}" archives lin-x64 library sha256)
file(SHA256 "${RACK_SDK_DIR}/libRack.so" _actual)
if(NOT _actual STREQUAL _expected)
  message(FATAL_ERROR "${RACK_SDK_DIR}/libRack.so has SHA-256 ${_actual}, not the pinned ${_expected}. ${_rackpy_sdk_help}")
endif()
set(_checked 0)
foreach(_section rack_headers dep_headers)
  string(JSON _count LENGTH "${_rackpy_provenance}" closure ${_section})
  math(EXPR _last "${_count} - 1")
  foreach(_i RANGE ${_last})
    string(JSON _header MEMBER "${_rackpy_provenance}" closure ${_section} ${_i})
    string(JSON _expected GET "${_rackpy_provenance}" closure ${_section} "${_header}")
    if(NOT EXISTS "${RACK_SDK_DIR}/${_header}")
      message(FATAL_ERROR "${RACK_SDK_DIR}/${_header} is missing. ${_rackpy_sdk_help}")
    endif()
    file(SHA256 "${RACK_SDK_DIR}/${_header}" _actual)
    if(NOT _actual STREQUAL _expected)
      message(FATAL_ERROR "${RACK_SDK_DIR}/${_header} does not match its pinned SHA-256. ${_rackpy_sdk_help}")
    endif()
    math(EXPR _checked "${_checked} + 1")
  endforeach()
endforeach()
message(STATUS "Rack SDK: ${RACK_SDK_DIR} (libRack.so and ${_checked} reachable headers match the pins)")

# compile.mk and plugin.mk for ARCH_LIN + ARCH_X64. -MMD -MP are left to CMake's
# dependency scanning. The SDK's -Wno-vla-extension is omitted: the headers are
# system includes here, so their warnings are suppressed anyway.
add_library(rackpy_sdk_compile INTERFACE)
target_include_directories(rackpy_sdk_compile SYSTEM INTERFACE "${RACK_SDK_DIR}/include" "${RACK_SDK_DIR}/dep/include")
target_compile_options(rackpy_sdk_compile INTERFACE
  -fPIC -g -O3 -funsafe-math-optimizations -fno-omit-frame-pointer -march=nehalem
  -Wall -Wextra -Wno-unused-parameter
  $<$<COMPILE_LANGUAGE:CXX>:-Wsuggest-override>
  $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)
if(RACKPY_WERROR)
  target_compile_options(rackpy_sdk_compile INTERFACE -Werror)
endif()
target_link_directories(rackpy_sdk_compile INTERFACE "${RACK_SDK_DIR}")
# libRack.so has no SONAME, so link it by name (-lRack): a full path would be
# recorded as the DT_NEEDED entry.
target_link_libraries(rackpy_sdk_compile INTERFACE Rack)

add_library(rackpy_sdk_plugin INTERFACE)
target_link_libraries(rackpy_sdk_plugin INTERFACE rackpy_sdk_compile)
# Rack symlinks /tmp/Rack2 to its system directory while it dlopens a plugin.
target_link_options(rackpy_sdk_plugin INTERFACE -Wl,-rpath=/tmp/Rack2 -static-libstdc++ -static-libgcc)

add_library(rackpy_sdk_program INTERFACE)
target_link_libraries(rackpy_sdk_program INTERFACE rackpy_sdk_compile)
target_link_options(rackpy_sdk_program INTERFACE "-Wl,-rpath,${RACK_SDK_DIR}" -static-libstdc++ -static-libgcc)
