# SPDX-License-Identifier: GPL-3.0-or-later
# Native build of the RackPy Rack plugin skeleton: the loader (plugin.so), the
# host library that embeds CPython, and native unit tests.
#
# bin/rackpy-dev fetches the pinned inputs and drives this file; see
# CONTRIBUTING.md. Configure by hand with:
#   cmake -S . -B build/dev -G Ninja -DRACK_SDK_DIR=... -DRACKPY_PYTHON_HOME=... -Dpybind11_DIR=...
cmake_minimum_required(VERSION 3.25)
project(RackPy VERSION 0.0.0 DESCRIPTION "Python for VCV Rack plugins" LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  message(FATAL_ERROR "RackPy builds only for Linux x86_64 so far; macOS and Windows are RP-40 and RP-41.")
endif()
if(CMAKE_BUILD_TYPE)
  # The SDK's flags already choose -O3 -g; a CMake build type would add its own.
  message(FATAL_ERROR "Leave CMAKE_BUILD_TYPE empty: RackPy uses the Rack SDK's optimization flags.")
endif()

option(RACKPY_WERROR "Treat warnings in RackPy sources as errors" OFF)
option(RACKPY_BUILD_TESTS "Build the native unit tests (they link the SDK's libRack and run outside Rack)" ON)
option(RACKPY_TEST_HOOKS "Add the RuntimeProbe module and _rackpy_host._testing (tests only; never ship this)" OFF)
set(RACKPY_ENTRY "" CACHE STRING "Trusted module the host imports at start-up, or empty")

include(cmake/RackSDK.cmake)
include(cmake/BundledPython.cmake)

# Log lines and debug info name sources relative to the repository, not the build machine's checkout.
add_compile_options("-ffile-prefix-map=${PROJECT_SOURCE_DIR}/=")

# Headers only: the host links the bundled libpython itself, not the build machine's Python.
set(PYBIND11_NOPYTHON ON)
find_package(pybind11 3.1.0 EXACT CONFIG)
if(NOT pybind11_FOUND)
  message(FATAL_ERROR "pybind11 3.1.0 not found. Run `uv sync --frozen` and pass -Dpybind11_DIR=$(uv run python -m pybind11 --cmakedir), or use bin/rackpy-dev.")
endif()

# plugin.so: thin loader with no Python dependency (ADR 002, decision 1).
add_library(rackpy_loader MODULE native/loader/loader.cpp native/loader/loader_config.cpp)
set_target_properties(rackpy_loader PROPERTIES
  PREFIX "" OUTPUT_NAME plugin
  CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
  SKIP_BUILD_RPATH ON)
target_include_directories(rackpy_loader PRIVATE native/include)
target_link_libraries(rackpy_loader PRIVATE rackpy_sdk_plugin)

# The runtime's native-only parts, shared by the host and the native unit tests.
set(RACKPY_RUNTIME_CORE native/runtime/threads.cpp native/runtime/diagnostics.cpp native/runtime/context.cpp)

# Rack API bindings: the registry bin/generate_bindings.py generates from bindings/manifest.json,
# and the hand-written adapters it calls (native/bindings/<family>.cpp).
file(GLOB_RECURSE RACKPY_BINDING_ADAPTERS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/native/bindings/*.cpp")
list(FILTER RACKPY_BINDING_ADAPTERS EXCLUDE REGEX "/native/bindings/generated/")

# rackpy_host.so: embeds CPython; exports only rackpy_host_init and rackpy_host_destroy.
add_library(rackpy_host MODULE native/host/host.cpp native/host/host_module.cpp
  ${RACKPY_RUNTIME_CORE} native/runtime/python.cpp native/runtime/interpreter.cpp native/runtime/json.cpp
  native/bindings/generated/registry.cpp ${RACKPY_BINDING_ADAPTERS})
if(RACKPY_TEST_HOOKS)
  target_sources(rackpy_host PRIVATE native/testing/testing.cpp)
  target_compile_definitions(rackpy_host PRIVATE RACKPY_TEST_HOOKS)
  target_include_directories(rackpy_host PRIVATE native/testing)
endif()
set_target_properties(rackpy_host PROPERTIES
  PREFIX ""
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
  BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN/python/lib")
target_include_directories(rackpy_host PRIVATE native/include native/bindings/generated)
target_link_libraries(rackpy_host PRIVATE rackpy_sdk_plugin rackpy_python pybind11::headers)
# libstdc++ gives std:: instances and its static objects default visibility, so
# -fvisibility=hidden is not enough; the version script exports the ABI only.
target_link_options(rackpy_host PRIVATE "-Wl,--version-script=${PROJECT_SOURCE_DIR}/native/host/host.map")
set_property(TARGET rackpy_host APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/native/host/host.map")

if(RACKPY_BUILD_TESTS)
  enable_testing()
  add_executable(rackpy_loader_tests native/tests/test_loader_config.cpp native/loader/loader_config.cpp)
  set_target_properties(rackpy_loader_tests PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(rackpy_loader_tests PRIVATE native/include native/loader)
  target_link_libraries(rackpy_loader_tests PRIVATE rackpy_sdk_program)
  add_test(NAME loader_config COMMAND rackpy_loader_tests)

  add_executable(rackpy_runtime_tests native/tests/test_runtime.cpp ${RACKPY_RUNTIME_CORE})
  set_target_properties(rackpy_runtime_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(rackpy_runtime_tests PRIVATE native/include)
  target_link_libraries(rackpy_runtime_tests PRIVATE rackpy_sdk_program)
  add_test(NAME runtime_core COMMAND rackpy_runtime_tests)

  add_executable(rackpy_module_tests native/tests/test_module.cpp ${RACKPY_RUNTIME_CORE})
  set_target_properties(rackpy_module_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(rackpy_module_tests PRIVATE native/include)
  target_link_libraries(rackpy_module_tests PRIVATE rackpy_sdk_program)
  add_test(NAME module_construction COMMAND rackpy_module_tests)

  add_executable(rackpy_widget_tests native/tests/test_widget_tree.cpp ${RACKPY_RUNTIME_CORE})
  set_target_properties(rackpy_widget_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(rackpy_widget_tests PRIVATE native/include)
  target_link_libraries(rackpy_widget_tests PRIVATE rackpy_sdk_program)
  add_test(NAME widget_tree COMMAND rackpy_widget_tests)

  # Rack's own results for the family 1 bindings; tests/values compares rackpy.raw against them inside Rack.
  add_executable(rackpy_values_reference native/tests/values_reference.cpp)
  set_target_properties(rackpy_values_reference PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(rackpy_values_reference PRIVATE rackpy_sdk_program)
  add_executable(rackpy_components_reference native/tests/components_reference.cpp)
  set_target_properties(rackpy_components_reference PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(rackpy_components_reference PRIVATE rackpy_sdk_program)
  add_executable(rackpy_module_reference native/tests/module_reference.cpp)
  set_target_properties(rackpy_module_reference PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(rackpy_module_reference PRIVATE rackpy_sdk_program)
endif()

# Plugin directory layout. bin/rackpy-dev bundle adds the runtime as python/.
# Rack rejects a plugin whose manifest and registered models differ, so the
# test-hooks build declares its probe module and says what it is.
if(RACKPY_TEST_HOOKS)
  set(RACKPY_MANIFEST_NAME "RackPy (test hooks build; do not distribute)")
  set(RACKPY_MANIFEST_MODULES "[{\"slug\": \"RuntimeProbe\", \"name\": \"Runtime probe\", \"description\": \"RackPy test hook\", \"tags\": []}]")
else()
  set(RACKPY_MANIFEST_NAME "RackPy")
  set(RACKPY_MANIFEST_MODULES "[]")
endif()
configure_file(packaging/plugin/plugin.json.in plugin.json @ONLY)
configure_file(packaging/plugin/rackpy-loader.json.in rackpy-loader.json @ONLY)
install(TARGETS rackpy_loader rackpy_host LIBRARY DESTINATION .)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plugin.json" "${CMAKE_CURRENT_BINARY_DIR}/rackpy-loader.json" LICENSE NOTICE DESTINATION .)
install(DIRECTORY src/rackpy DESTINATION pythonpath
  FILES_MATCHING PATTERN "*.py" PATTERN "py.typed"
  PATTERN "__pycache__" EXCLUDE)
