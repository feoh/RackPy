# SPDX-License-Identifier: GPL-3.0-or-later
# Native build of the RackPy Rack plugin skeleton: the loader (plugin.so), the
# host library that embeds CPython, and native unit tests.
#
# bin/rackpy-dev fetches the pinned inputs and drives this file; see
# CONTRIBUTING.md. Configure by hand with:
#   cmake -S . -B build/dev -G Ninja -DRACK_SDK_DIR=... -DRACKPY_PYTHON_HOME=... -Dpybind11_DIR=...
cmake_minimum_required(VERSION 3.25)
# macOS: one architecture per build, and the oldest macOS both Rack 2.6.6 and the bundled CPython run on
# (RP-40): Rack's libRack.dylib needs 11.0 on arm64 and 10.9 on x86_64, CPython 3.14's 11.0 and 10.15.
if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS the plugin loads on")
elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Oldest macOS the plugin loads on")
endif()
project(RackPy VERSION 0.0.0 DESCRIPTION "Python for VCV Rack plugins" LANGUAGES C CXX)

if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$") AND NOT APPLE)
  message(FATAL_ERROR "RackPy builds for Linux x86_64 and macOS (x86_64, arm64) so far; Windows is RP-41.")
endif()
if(CMAKE_BUILD_TYPE)
  # The SDK's flags already choose -O3 -g; a CMake build type would add its own.
  message(FATAL_ERROR "Leave CMAKE_BUILD_TYPE empty: RackPy uses the Rack SDK's optimization flags.")
endif()

option(RACKPY_WERROR "Treat warnings in RackPy sources as errors" OFF)
option(RACKPY_BUILD_TESTS "Build the native unit tests (they link the SDK's libRack and run outside Rack)" ON)
option(RACKPY_TEST_HOOKS "Add the RuntimeProbe module and _rackpy_host._testing (tests only; never ship this)" OFF)
set(RACKPY_ENTRY "" CACHE STRING "Trusted module the host imports at start-up, or empty")
set(RACKPY_SANITIZE "" CACHE STRING "Sanitizers for every native target: empty, address,undefined or thread (tests only)")
set_property(CACHE RACKPY_SANITIZE PROPERTY STRINGS "" "address,undefined" "thread")
if(RACKPY_SANITIZE AND NOT RACKPY_SANITIZE MATCHES "^(address,undefined|thread)$")
  message(FATAL_ERROR "RACKPY_SANITIZE must be address,undefined or thread, not ${RACKPY_SANITIZE}")
endif()

include(cmake/RackSDK.cmake)
include(cmake/BundledPython.cmake)

# Log lines and debug info name sources relative to the repository, not the build machine's checkout,
# and the pinned inputs' headers by what they are, not where this machine cached them (RP-39).
add_compile_options("-ffile-prefix-map=${PROJECT_SOURCE_DIR}/=" "-ffile-prefix-map=${RACK_SDK_DIR}=Rack-SDK"
  "-ffile-prefix-map=${RACKPY_PYTHON_HOME}=python")

# Headers only: the host links the bundled libpython itself, not the build machine's Python.
set(PYBIND11_NOPYTHON ON)
find_package(pybind11 3.1.0 EXACT CONFIG)
if(NOT pybind11_FOUND)
  message(FATAL_ERROR "pybind11 3.1.0 not found. Run `uv sync --frozen` and pass -Dpybind11_DIR=$(uv run python -m pybind11 --cmakedir), or use bin/rackpy-dev.")
endif()

# plugin.so (plugin.dylib on macOS, as Rack looks for): thin loader with no Python dependency
# (ADR 002, decision 1).
add_library(rackpy_loader MODULE native/loader/loader.cpp native/loader/loader_config.cpp)
set_target_properties(rackpy_loader PROPERTIES
  PREFIX "" OUTPUT_NAME plugin
  CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
  SKIP_BUILD_RPATH ON)
if(APPLE)
  set_target_properties(rackpy_loader PROPERTIES SUFFIX ".dylib")
endif()
target_include_directories(rackpy_loader PRIVATE native/include)
target_link_libraries(rackpy_loader PRIVATE rackpy_sdk_plugin)

# The runtime's native-only parts, shared by the host and the native unit tests.
set(RACKPY_RUNTIME_CORE native/runtime/threads.cpp native/runtime/diagnostics.cpp native/runtime/context.cpp
  native/engine/expanders.cpp native/io/audio_bridge.cpp native/io/endpoints.cpp native/io/midi_queue.cpp)
# The kernel runtime (RP-17A): Python-free, and the production audio path.
set(RACKPY_DSP_RUNTIME native/dsp/runtime/runtime.cpp native/dsp/runtime/builtin.cpp native/dsp/graph/plan.cpp)

# Rack API bindings: the registry bin/generate_bindings.py generates from bindings/manifest.json,
# and the hand-written adapters it calls (native/bindings/<family>.cpp).
file(GLOB_RECURSE RACKPY_BINDING_ADAPTERS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/native/bindings/*.cpp")
list(FILTER RACKPY_BINDING_ADAPTERS EXCLUDE REGEX "/native/bindings/generated/")
# The callback laboratory (RP-22): offline Python callbacks, never on Rack's engine threads.
file(GLOB RACKPY_LABORATORY CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/native/laboratory/*.cpp")

# rackpy_host.so: embeds CPython; exports only rackpy_host_init and rackpy_host_destroy.
add_library(rackpy_host MODULE native/host/host.cpp native/host/host_module.cpp
  ${RACKPY_RUNTIME_CORE} native/runtime/python.cpp native/runtime/interpreter.cpp native/runtime/json.cpp
  native/bindings/generated/registry.cpp ${RACKPY_BINDING_ADAPTERS} native/module/lifecycle.cpp
  native/plugin/model.cpp ${RACKPY_DSP_RUNTIME} native/dsp/runtime/libraries.cpp ${RACKPY_LABORATORY})
# Compiling a binding translation unit takes up to about 5 GB (pybind11 templates: dsp/filter.cpp and
# dsp/resampler.cpp measured 4.8 GB with GCC 16), so parallel compiles are capped by memory rather than
# by cores: one per 6 GB, which is 2 on a 16 GB CI runner. Ninja job pools; other generators ignore it.
cmake_host_system_information(RESULT _rackpy_memory_mib QUERY TOTAL_PHYSICAL_MEMORY)
math(EXPR _rackpy_compile_jobs "${_rackpy_memory_mib} / 6144")
if(_rackpy_compile_jobs LESS 1)
  set(_rackpy_compile_jobs 1)
endif()
set(RACKPY_COMPILE_JOBS ${_rackpy_compile_jobs} CACHE STRING "Parallel compiles of RackPy's heavy sources (memory-bound)")
set_property(GLOBAL APPEND PROPERTY JOB_POOLS rackpy_heavy=${RACKPY_COMPILE_JOBS})
set_property(TARGET rackpy_host PROPERTY JOB_POOL_COMPILE rackpy_heavy)
if(RACKPY_TEST_HOOKS)
  target_sources(rackpy_host PRIVATE native/testing/testing.cpp)
  target_compile_definitions(rackpy_host PRIVATE RACKPY_TEST_HOOKS)
  target_include_directories(rackpy_host PRIVATE native/testing)
endif()
set_target_properties(rackpy_host PROPERTIES
  PREFIX ""
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
  BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$<IF:$<BOOL:${APPLE}>,@loader_path/python/lib,$ORIGIN/python/lib>")
target_include_directories(rackpy_host PRIVATE native/include native/bindings/generated)
target_link_libraries(rackpy_host PRIVATE rackpy_sdk_plugin rackpy_python pybind11::headers)
# libstdc++ gives std:: instances and its static objects default visibility, so
# -fvisibility=hidden is not enough; the version script (on macOS the exported symbols list)
# exports the ABI only.
if(APPLE)
  set(_rackpy_host_exports "${PROJECT_SOURCE_DIR}/native/host/host.exp")
  target_link_options(rackpy_host PRIVATE "-Wl,-exported_symbols_list,${_rackpy_host_exports}")
else()
  set(_rackpy_host_exports "${PROJECT_SOURCE_DIR}/native/host/host.map")
  target_link_options(rackpy_host PRIVATE "-Wl,--version-script=${_rackpy_host_exports}")
endif()
set_property(TARGET rackpy_host APPEND PROPERTY LINK_DEPENDS "${_rackpy_host_exports}")

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

  add_executable(rackpy_module_tests native/tests/test_module.cpp native/module/lifecycle.cpp ${RACKPY_RUNTIME_CORE}
    ${RACKPY_DSP_RUNTIME})
  set_target_properties(rackpy_module_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(rackpy_module_tests PRIVATE native/include)
  target_link_libraries(rackpy_module_tests PRIVATE rackpy_sdk_program)
  add_test(NAME module_construction COMMAND rackpy_module_tests)

  add_executable(rackpy_kernel_tests native/tests/test_kernels.cpp native/module/lifecycle.cpp ${RACKPY_RUNTIME_CORE}
    ${RACKPY_DSP_RUNTIME})
  set_target_properties(rackpy_kernel_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(rackpy_kernel_tests PRIVATE native/include)
  target_link_libraries(rackpy_kernel_tests PRIVATE rackpy_sdk_program)
  add_test(NAME kernel_runtime COMMAND rackpy_kernel_tests)
  # RP-21B: graph plans against direct C++ twins.
  add_executable(rackpy_graph_tests native/tests/test_graph.cpp native/module/lifecycle.cpp ${RACKPY_RUNTIME_CORE}
    ${RACKPY_DSP_RUNTIME})
  set_target_properties(rackpy_graph_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(rackpy_graph_tests PRIVATE native/include)
  target_link_libraries(rackpy_graph_tests PRIVATE rackpy_sdk_program)
  add_test(NAME graph_plans COMMAND rackpy_graph_tests)
  # RP-31J: audio-port and MIDI-queue I/O against Core Audio's and Rack's InputQueue's own code.
  add_executable(rackpy_io_tests native/tests/test_io.cpp native/module/lifecycle.cpp ${RACKPY_RUNTIME_CORE}
    ${RACKPY_DSP_RUNTIME})
  set_target_properties(rackpy_io_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(rackpy_io_tests PRIVATE native/include)
  target_link_libraries(rackpy_io_tests PRIVATE rackpy_sdk_program)
  add_test(NAME io_paths COMMAND rackpy_io_tests)
  # Dedicated sanitizer builds; a RACKPY_SANITIZE build instruments every target instead (RP-38).
  if(NOT RACKPY_SANITIZE)
    # RP-23C: expander exchange under stress (tests/engine/test_expander_stress.py), once with ASan and UBSan,
    # once with TSan; libRack is not instrumented.
    foreach(_variant asan tsan)
      add_executable(rackpy_expander_stress_${_variant} native/tests/expander_stress.cpp native/module/lifecycle.cpp
        ${RACKPY_RUNTIME_CORE} ${RACKPY_DSP_RUNTIME})
      set_target_properties(rackpy_expander_stress_${_variant} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
      target_include_directories(rackpy_expander_stress_${_variant} PRIVATE native/include)
      target_link_libraries(rackpy_expander_stress_${_variant} PRIVATE rackpy_sdk_program)
      set_property(TARGET rackpy_expander_stress_${_variant} PROPERTY JOB_POOL_COMPILE rackpy_heavy)
    endforeach()
    target_compile_options(rackpy_expander_stress_asan PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined
      -fno-omit-frame-pointer)
    target_link_options(rackpy_expander_stress_asan PRIVATE -fsanitize=address,undefined)
    # -Wno-tsan: the diagnostics ring (native/runtime/diagnostics.cpp) uses fences, which TSan does not model;
    # the expander snapshot does not.
    target_compile_options(rackpy_expander_stress_tsan PRIVATE -fsanitize=thread -fno-omit-frame-pointer $<$<CXX_COMPILER_ID:GNU>:-Wno-tsan>)
    target_link_options(rackpy_expander_stress_tsan PRIVATE -fsanitize=thread)
    # RP-31J: a module's audio port and MIDI inputs under stress (tests/io/test_io_native.py), the same two ways.
    foreach(_variant asan tsan)
      add_executable(rackpy_io_stress_${_variant} native/tests/io_stress.cpp ${RACKPY_RUNTIME_CORE})
      set_target_properties(rackpy_io_stress_${_variant} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
      target_include_directories(rackpy_io_stress_${_variant} PRIVATE native/include)
      target_link_libraries(rackpy_io_stress_${_variant} PRIVATE rackpy_sdk_program)
    endforeach()
    target_compile_options(rackpy_io_stress_asan PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined
      -fno-omit-frame-pointer)
    target_link_options(rackpy_io_stress_asan PRIVATE -fsanitize=address,undefined)
    target_compile_options(rackpy_io_stress_tsan PRIVATE -fsanitize=thread -fno-omit-frame-pointer $<$<CXX_COMPILER_ID:GNU>:-Wno-tsan>)
    target_link_options(rackpy_io_stress_tsan PRIVATE -fsanitize=thread)
  endif()
  # The RP-07 audio-path guard interposes on libc with LD_PRELOAD, which macOS has no equivalent of
  # for these tests (DYLD_INSERT_LIBRARIES is limited by SIP): Linux only (RP-40).
  if(NOT APPLE)
    # The RP-07 audio-path guard, preloaded by tests/dsp/test_runtime.py around rackpy_kernel_guard.
    add_library(rackpy_audio_guard MODULE native/testing/audio_guard.c)
    set_target_properties(rackpy_audio_guard PROPERTIES PREFIX "" OUTPUT_NAME audio_guard C_STANDARD 11)
    target_link_libraries(rackpy_audio_guard PRIVATE dl)
    # Runs the kernel path under the RP-07 audio-path guard (tests/dsp/test_runtime.py preloads it).
    add_executable(rackpy_kernel_guard native/tests/kernel_guard.cpp tests/dsp/kernel_library/graphs.cpp
      native/module/lifecycle.cpp ${RACKPY_RUNTIME_CORE} ${RACKPY_DSP_RUNTIME})
    set_target_properties(rackpy_kernel_guard PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_include_directories(rackpy_kernel_guard PRIVATE native/include)
    target_link_libraries(rackpy_kernel_guard PRIVATE rackpy_sdk_program)
    # RP-36: the example plugins' kernel libraries under the same guard (tests/examples preloads it).
    add_executable(rackpy_example_kernel_guard native/tests/example_kernel_guard.cpp)
    set_target_properties(rackpy_example_kernel_guard PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_include_directories(rackpy_example_kernel_guard PRIVATE native/include)
    target_link_libraries(rackpy_example_kernel_guard PRIVATE rackpy_sdk_program dl)
  endif()

  # RackPy's kernel path against plain C++ modules, per frame (timing only; not a CTest).
  add_executable(rackpy_kernel_bench native/tests/kernel_bench.cpp native/module/lifecycle.cpp ${RACKPY_RUNTIME_CORE}
    ${RACKPY_DSP_RUNTIME})
  set_target_properties(rackpy_kernel_bench PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(rackpy_kernel_bench PRIVATE native/include)
  target_link_libraries(rackpy_kernel_bench PRIVATE rackpy_sdk_program)

  # RP-21B/C: the plan interpreter and the compiled tier (bin/rackpy-graph's tests/dsp/kernel_library/graphs.cpp)
  # against hand-fused C++ modules (timing only; not a CTest).
  add_executable(rackpy_graph_bench native/tests/graph_bench.cpp tests/dsp/kernel_library/graphs.cpp
    native/module/lifecycle.cpp ${RACKPY_RUNTIME_CORE} ${RACKPY_DSP_RUNTIME})
  set_target_properties(rackpy_graph_bench PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(rackpy_graph_bench PRIVATE native/include)
  target_link_libraries(rackpy_graph_bench PRIVATE rackpy_sdk_program)

  # Kernel libraries for tests/dsp (RP-17B): the example, and ones RackPy must refuse.
  set(_rackpy_kernel_libraries example:fold.cpp:NONE wrong_abi:broken.cpp:VARIANT_WRONG_ABI
    throws:broken.cpp:VARIANT_THROWS duplicate:broken.cpp:VARIANT_DUPLICATE reserved:broken.cpp:VARIANT_RESERVED
    bad_name:broken.cpp:VARIANT_BAD_NAME graph_wrong_abi:broken.cpp:VARIANT_GRAPH_WRONG_ABI
    graph_bad_node:broken.cpp:VARIANT_GRAPH_BAD_NODE graphs:graphs.cpp:NONE levels:levels.cpp:NONE
    expander_wrong_abi:broken.cpp:VARIANT_EXPANDER_WRONG_ABI expander_partial:broken.cpp:VARIANT_EXPANDER_PARTIAL
    io:io.cpp:NONE io_wrong_abi:broken.cpp:VARIANT_IO_WRONG_ABI io_partial:broken.cpp:VARIANT_IO_PARTIAL)
  foreach(_entry IN LISTS _rackpy_kernel_libraries)
    string(REPLACE ":" ";" _parts "${_entry}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _source)
    list(GET _parts 2 _variant)
    add_library(rackpy_kernels_${_name} MODULE tests/dsp/kernel_library/${_source})
    set_target_properties(rackpy_kernels_${_name} PROPERTIES PREFIX "" OUTPUT_NAME kernels_${_name}
      CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF CXX_VISIBILITY_PRESET hidden)
    target_include_directories(rackpy_kernels_${_name} PRIVATE native/include)
    target_link_libraries(rackpy_kernels_${_name} PRIVATE rackpy_sdk_plugin)
    if(NOT _variant STREQUAL "NONE")
      target_compile_definitions(rackpy_kernels_${_name} PRIVATE ${_variant})
    endif()
  endforeach()

  add_executable(rackpy_widget_tests native/tests/test_widget_tree.cpp ${RACKPY_RUNTIME_CORE})
  set_target_properties(rackpy_widget_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_include_directories(rackpy_widget_tests PRIVATE native/include)
  target_link_libraries(rackpy_widget_tests PRIVATE rackpy_sdk_program)
  add_test(NAME widget_tree COMMAND rackpy_widget_tests)

  # Rack's own results for the family 1 bindings; tests/values compares rackpy.raw against them inside Rack.
  add_executable(rackpy_values_reference native/tests/values_reference.cpp)
  set_target_properties(rackpy_values_reference PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(rackpy_values_reference PRIVATE rackpy_sdk_program)
  # Rack's own SIMD results (RP-20) with SSE4.2, as Rack builds for x64, and through simd/common.hpp's
  # SIMDe branch with SIMDe's portable code (no SSE3 or later, SIMDE_NO_NATIVE). tests/simd compares
  # rackpy.raw.simd with the first inside Rack, and the two implementations with each other.
  add_executable(rackpy_simd_reference native/tests/simd_reference.cpp)
  set_target_properties(rackpy_simd_reference PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(rackpy_simd_reference PRIVATE rackpy_sdk_program)
  add_executable(rackpy_simd_reference_simde native/tests/simd_reference.cpp)
  set_target_properties(rackpy_simd_reference_simde PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  # Explicit -mno-* flags win over the SDK's -march=nehalem whatever their order. -mno-sse3 (which
  # also drops SSSE3 and SSE4) keeps libstdc++'s <random> from including the native pmmintrin.h,
  # whose __m128i would clash with SIMDe's. SIMDe's headers trip GCC's maybe-uninitialized analysis.
  # On arm64 Rack's SIMD types are SIMDe already, and SIMDE_NO_NATIVE gives its portable code there.
  target_compile_options(rackpy_simd_reference_simde PRIVATE
    $<$<NOT:$<STREQUAL:${RACKPY_PLATFORM},mac-arm64>>:-mno-sse3>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>)
  target_compile_definitions(rackpy_simd_reference_simde PRIVATE SIMDE_NO_NATIVE)
  target_link_libraries(rackpy_simd_reference_simde PRIVATE rackpy_sdk_program)
  # Rack's own DSP results (RP-18); tests/dsp compares rackpy.raw.dsp against them inside Rack.
  add_executable(rackpy_dsp_reference native/tests/dsp_reference.cpp)
  set_target_properties(rackpy_dsp_reference PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(rackpy_dsp_reference PRIVATE rackpy_sdk_program)
  # The same reference under AddressSanitizer and UndefinedBehaviorSanitizer (tests/dsp/test_sanitizers.py):
  # every buffer boundary it exercises in Rack's inline DSP code is checked. libRack is not instrumented.
  set_property(TARGET rackpy_dsp_reference PROPERTY JOB_POOL_COMPILE rackpy_heavy)
  if(NOT RACKPY_SANITIZE)
    add_executable(rackpy_dsp_reference_sanitized native/tests/dsp_reference.cpp)
    set_target_properties(rackpy_dsp_reference_sanitized PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_compile_options(rackpy_dsp_reference_sanitized PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined)
    target_link_options(rackpy_dsp_reference_sanitized PRIVATE -fsanitize=address,undefined)
    target_link_libraries(rackpy_dsp_reference_sanitized PRIVATE rackpy_sdk_program)
    set_property(TARGET rackpy_dsp_reference_sanitized PROPERTY JOB_POOL_COMPILE rackpy_heavy)
  endif()
  add_executable(rackpy_components_reference native/tests/components_reference.cpp)
  set_target_properties(rackpy_components_reference PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(rackpy_components_reference PRIVATE rackpy_sdk_program)
  add_executable(rackpy_module_reference native/tests/module_reference.cpp)
  set_target_properties(rackpy_module_reference PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(rackpy_module_reference PRIVATE rackpy_sdk_program)
  # The C++ reference that examples/oscillator is compared with (RP-33).
  add_executable(rackpy_oscillator_reference native/tests/oscillator_reference.cpp)
  set_target_properties(rackpy_oscillator_reference PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(rackpy_oscillator_reference PRIVATE rackpy_sdk_program)
  # RP-36: examples/polyphony's oscillator as C++, scalar and float_4.
  add_executable(rackpy_polyphony_reference native/tests/polyphony_reference.cpp)
  set_target_properties(rackpy_polyphony_reference PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  target_link_libraries(rackpy_polyphony_reference PRIVATE rackpy_sdk_program)
endif()

# Example plugins' kernel libraries (RP-36): examples/<project>/kernels/<name>.cpp becomes
# examples/<project>/kernels/<name>.so in the build tree, built as RackPy's own kernel libraries are
# (the SDK's plugin flags, RackPy's native/include). bin/rackpy-plugin build --kernels copies them in.
file(GLOB RACKPY_EXAMPLE_KERNELS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/examples/*/kernels/*.cpp")
foreach(_source IN LISTS RACKPY_EXAMPLE_KERNELS)
  get_filename_component(_name "${_source}" NAME_WE)
  get_filename_component(_kernels "${_source}" DIRECTORY)
  get_filename_component(_project_dir "${_kernels}" DIRECTORY)
  get_filename_component(_project "${_project_dir}" NAME)
  string(MAKE_C_IDENTIFIER "rackpy_example_${_project}_${_name}" _target)
  add_library(${_target} MODULE "${_source}")
  set_target_properties(${_target} PROPERTIES PREFIX "" OUTPUT_NAME "${_name}"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/examples/${_project}/kernels"
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF CXX_VISIBILITY_PRESET hidden
    SKIP_BUILD_RPATH ON)  # Rack loads libRack.so through /tmp/Rack2, as for plugin.so; not the SDK path
  target_include_directories(${_target} PRIVATE native/include)
  target_link_libraries(${_target} PRIVATE rackpy_sdk_plugin)
endforeach()

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
# The loader refuses a configuration whose "abi" differs from the header's, so take it from there.
file(STRINGS native/include/rackpy/host_abi.h _rackpy_abi_line REGEX "^#define RACKPY_HOST_ABI [0-9]+$")
string(REGEX REPLACE "^#define RACKPY_HOST_ABI " "" RACKPY_HOST_ABI "${_rackpy_abi_line}")
configure_file(packaging/plugin/rackpy-loader.json.in rackpy-loader.json @ONLY)
# Explicit modes: on Debian and Ubuntu CMake installs shared libraries without execute bits
# (CMAKE_INSTALL_SO_NO_EXE), which made the same release differ between build machines (RP-39).
install(TARGETS rackpy_loader rackpy_host LIBRARY DESTINATION .
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/plugin.json" "${CMAKE_CURRENT_BINARY_DIR}/rackpy-loader.json" LICENSE NOTICE DESTINATION .)
install(DIRECTORY src/rackpy DESTINATION pythonpath
  FILES_MATCHING PATTERN "*.py" PATTERN "py.typed"
  PATTERN "__pycache__" EXCLUDE)
