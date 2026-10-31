# SPDX-License-Identifier: GPL-3.0-or-later
# The pinned Rack SDK and the flags its compile.mk/plugin.mk give Linux x64 and macOS (x64, arm64) plugins.
#
# Defines:
#   RACKPY_PLATFORM     lin-x64, mac-x64 or mac-arm64: Rack's name for the target
#   rackpy_sdk_compile  compile flags and SDK headers (as -isystem, see ADR 004)
#   rackpy_sdk_plugin   compile + link flags for a DSO that Rack loads
#   rackpy_sdk_program  compile + link flags for a native test program outside Rack

if(APPLE)
  if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    set(RACKPY_PLATFORM mac-arm64)
  elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    set(RACKPY_PLATFORM mac-x64)
  else()
    message(FATAL_ERROR "Set CMAKE_OSX_ARCHITECTURES to arm64 or x86_64 (one architecture per build), not \"${CMAKE_OSX_ARCHITECTURES}\".")
  endif()
  set(RACK_LIBRARY_NAME libRack.dylib)
else()
  set(RACKPY_PLATFORM lin-x64)
  set(RACK_LIBRARY_NAME libRack.so)
endif()

set(RACK_SDK_DIR "" CACHE PATH "Extracted, verified Rack SDK for ${RACKPY_PLATFORM} (print it with: bin/rack-sdk path ${RACKPY_PLATFORM})")

set(_rackpy_sdk_help "Fetch and verify the pinned SDK with `bin/rack-sdk path`, then configure with -DRACK_SDK_DIR=<that path>, or let `bin/rackpy-dev` do both.")
if(NOT RACK_SDK_DIR)
  message(FATAL_ERROR "RACK_SDK_DIR is not set. ${_rackpy_sdk_help}")
endif()
if(NOT EXISTS "${RACK_SDK_DIR}/include/rack.hpp" OR NOT EXISTS "${RACK_SDK_DIR}/${RACK_LIBRARY_NAME}")
  message(FATAL_ERROR "RACK_SDK_DIR=${RACK_SDK_DIR} is not an extracted ${RACKPY_PLATFORM} Rack SDK (no include/rack.hpp or ${RACK_LIBRARY_NAME}). ${_rackpy_sdk_help}")
endif()

# The SDK must be the pinned one: check libRack and every header reachable
# from rack.hpp against tests/fixtures/sdk-provenance.json (RP-02).
file(READ "${PROJECT_SOURCE_DIR}/tests/fixtures/sdk-provenance.json" _rackpy_provenance)
string(JSON _expected GET "${_rackpy_provenance}" archives ${RACKPY_PLATFORM} library sha256)
file(SHA256 "${RACK_SDK_DIR}/${RACK_LIBRARY_NAME}" _actual)
if(NOT _actual STREQUAL _expected)
  message(FATAL_ERROR "${RACK_SDK_DIR}/${RACK_LIBRARY_NAME} has SHA-256 ${_actual}, not the pinned ${_expected}. ${_rackpy_sdk_help}")
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
message(STATUS "Rack SDK: ${RACK_SDK_DIR} (${RACK_LIBRARY_NAME} and ${_checked} reachable headers match the pins)")

# compile.mk and plugin.mk for ARCH_LIN or ARCH_MAC, with ARCH_X64 or ARCH_ARM64. -MMD -MP are left
# to CMake's dependency scanning, and -mmacosx-version-min to CMAKE_OSX_DEPLOYMENT_TARGET. The SDK's
# -Wno-vla-extension is omitted: the headers are system includes here, so their warnings are
# suppressed anyway.
if(RACKPY_PLATFORM STREQUAL "mac-arm64")
  set(_rackpy_march -march=armv8-a+fp+simd)
else()
  set(_rackpy_march -march=nehalem)
endif()
add_library(rackpy_sdk_compile INTERFACE)
target_include_directories(rackpy_sdk_compile SYSTEM INTERFACE "${RACK_SDK_DIR}/include" "${RACK_SDK_DIR}/dep/include")
target_compile_options(rackpy_sdk_compile INTERFACE
  -fPIC -g -O3 -funsafe-math-optimizations -fno-omit-frame-pointer ${_rackpy_march}
  -Wall -Wextra -Wno-unused-parameter
  $<$<COMPILE_LANGUAGE:CXX>:-Wsuggest-override>
  $<$<AND:$<BOOL:${APPLE}>,$<COMPILE_LANGUAGE:CXX>>:-stdlib=libc++>
  $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)
if(RACKPY_WERROR)
  target_compile_options(rackpy_sdk_compile INTERFACE -Werror)
endif()
# RP-38: a sanitizer build of every native target ("address,undefined" or "thread"), for the native
# tests (CTest). Never distribute one: Rack itself is not instrumented, so a sanitized plugin loads only
# with the sanitizer runtime preloaded.
if(RACKPY_SANITIZE)
  target_compile_options(rackpy_sdk_compile INTERFACE -fsanitize=${RACKPY_SANITIZE} -fno-omit-frame-pointer)
  if(RACKPY_SANITIZE STREQUAL "address,undefined")
    target_compile_options(rackpy_sdk_compile INTERFACE -fno-sanitize-recover=undefined)
  endif()
  target_link_options(rackpy_sdk_compile INTERFACE -fsanitize=${RACKPY_SANITIZE})
endif()
add_library(rackpy_sdk_plugin INTERFACE)
target_link_libraries(rackpy_sdk_plugin INTERFACE rackpy_sdk_compile)
add_library(rackpy_sdk_program INTERFACE)
target_link_libraries(rackpy_sdk_program INTERFACE rackpy_sdk_compile)
if(APPLE)
  # The SDK's libRack.dylib names itself "libRack.dylib", which VCV's `make dist` rewrites in each plugin
  # to /tmp/Rack2/libRack.dylib (Rack symlinks /tmp/Rack2 to its own folder while it loads plugins).
  # Linking against copies whose install names are already the right ones gives the same load commands
  # without editing a linked, signed binary: /tmp/Rack2/... for plugins, @rpath/... for test programs.
  find_program(RACKPY_INSTALL_NAME_TOOL NAMES install_name_tool llvm-install-name-tool REQUIRED)
  foreach(_kind plugin program)
    if(_kind STREQUAL "plugin")
      set(_id /tmp/Rack2/libRack.dylib)
    else()
      set(_id @rpath/libRack.dylib)
    endif()
    set(_copy "${PROJECT_BINARY_DIR}/rack-link/${_kind}/libRack.dylib")
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/rack-link/${_kind}")
    file(COPY_FILE "${RACK_SDK_DIR}/libRack.dylib" "${_copy}" ONLY_IF_DIFFERENT)
    execute_process(COMMAND "${RACKPY_INSTALL_NAME_TOOL}" -id "${_id}" "${_copy}" RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(_rc)
      message(FATAL_ERROR "${RACKPY_INSTALL_NAME_TOOL} -id ${_id} ${_copy} failed: ${_err}")
    endif()
    target_link_libraries(rackpy_sdk_${_kind} INTERFACE "${_copy}")
  endforeach()
  # plugin.mk: symbols Rack provides are looked up when the plugin loads.
  target_link_options(rackpy_sdk_plugin INTERFACE -undefined dynamic_lookup)
  # plugin.mk links plugins with -shared, a dylib (MH_DYLIB), where CMake makes MODULE libraries
  # bundles (MH_BUNDLE, -bundle): link RackPy's modules (plugin.dylib, rackpy_host.so, kernels) as
  # Rack's own plugins are.
  set(CMAKE_SHARED_MODULE_CREATE_C_FLAGS "-dynamiclib -Wl,-headerpad_max_install_names")
  set(CMAKE_SHARED_MODULE_CREATE_CXX_FLAGS "-dynamiclib -Wl,-headerpad_max_install_names")
  target_link_options(rackpy_sdk_program INTERFACE "-Wl,-rpath,${RACK_SDK_DIR}")
else()
  target_link_directories(rackpy_sdk_compile INTERFACE "${RACK_SDK_DIR}")
  # libRack.so has no SONAME, so link it by name (-lRack): a full path would be
  # recorded as the DT_NEEDED entry.
  target_link_libraries(rackpy_sdk_compile INTERFACE Rack)
  # Rack symlinks /tmp/Rack2 to its system directory while it dlopens a plugin.
  target_link_options(rackpy_sdk_plugin INTERFACE -Wl,-rpath=/tmp/Rack2 -static-libstdc++ -static-libgcc)
  target_link_options(rackpy_sdk_program INTERFACE "-Wl,-rpath,${RACK_SDK_DIR}" -static-libstdc++ -static-libgcc)
endif()

# A macOS module's install name (LC_ID_DYLIB) is, by default, the path the linker wrote it to: a build-tree
# path, which would ship in every plugin. Rack loads plugins by path, so the name is only a label; give each
# module @rpath/<its file name> instead. Call after the target's OUTPUT_NAME and SUFFIX are set.
function(rackpy_module_install_name target)
  if(APPLE)
    get_target_property(_output ${target} OUTPUT_NAME)
    get_target_property(_suffix ${target} SUFFIX)
    if(NOT _output)
      set(_output ${target})
    endif()
    if(NOT _suffix)
      set(_suffix "${CMAKE_SHARED_MODULE_SUFFIX}")
    endif()
    target_link_options(${target} PRIVATE "LINKER:-install_name,@rpath/${_output}${_suffix}")
  endif()
endfunction()
