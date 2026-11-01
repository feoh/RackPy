# SPDX-License-Identifier: GPL-3.0-or-later
# The bundled CPython runtime the host links against and the plugin ships (ADR 002, decisions 2 and 3).
#
# Defines rackpy_python: headers and libpython from RACKPY_PYTHON_HOME, a
# python-build-standalone install_only tree pinned in packaging/inputs.json.

set(RACKPY_PYTHON_VERSION "3.14" CACHE STRING "CPython minor version of the bundled runtime")
set(RACKPY_PYTHON_HOME "" CACHE PATH "Pinned python-build-standalone runtime (print it with: bin/rackpy-dev inputs)")

set(_rackpy_python_help "Fetch the pinned runtime with `bin/rackpy-dev inputs` and pass -DRACKPY_PYTHON_HOME=<python-${RACKPY_PYTHON_VERSION}>, or let `bin/rackpy-dev` configure the build.")
if(NOT RACKPY_PYTHON_HOME)
  message(FATAL_ERROR "RACKPY_PYTHON_HOME is not set. ${_rackpy_python_help}")
endif()
set(RACKPY_PYTHON_INCLUDE "include/python${RACKPY_PYTHON_VERSION}")
set(RACKPY_STDLIB_MARKER "lib/python${RACKPY_PYTHON_VERSION}/os.py")
if(APPLE)
  set(RACKPY_LIBPYTHON "lib/libpython${RACKPY_PYTHON_VERSION}.dylib")
elseif(WIN32)
  # The Windows layout: python314.dll (MSVC-built, UCRT) and vcruntime140.dll at the top, Lib\, DLLs\, include\.
  string(REPLACE "." "" _rackpy_python_nodot "${RACKPY_PYTHON_VERSION}")
  set(RACKPY_LIBPYTHON "python${_rackpy_python_nodot}.dll")
  set(RACKPY_PYTHON_INCLUDE "include")
  set(RACKPY_STDLIB_MARKER "Lib/os.py")
else()
  set(RACKPY_LIBPYTHON "lib/libpython${RACKPY_PYTHON_VERSION}.so.1.0")
endif()
foreach(_file "${RACKPY_PYTHON_INCLUDE}/Python.h" "${RACKPY_LIBPYTHON}" "${RACKPY_STDLIB_MARKER}")
  if(NOT EXISTS "${RACKPY_PYTHON_HOME}/${_file}")
    message(FATAL_ERROR "${RACKPY_PYTHON_HOME} is not a CPython ${RACKPY_PYTHON_VERSION} runtime (no ${_file}). ${_rackpy_python_help}")
  endif()
endforeach()
message(STATUS "Bundled CPython ${RACKPY_PYTHON_VERSION}: ${RACKPY_PYTHON_HOME}")

add_library(rackpy_python INTERFACE)
target_include_directories(rackpy_python SYSTEM INTERFACE "${RACKPY_PYTHON_HOME}/${RACKPY_PYTHON_INCLUDE}")
# Link the exact file; its SONAME (libpython3.14.so.1.0) becomes DT_NEEDED, or on macOS its install
# name (@rpath/libpython3.14.dylib) LC_LOAD_DYLIB, and the host's RUNPATH or LC_RPATH finds it in the
# plugin's python/lib. On Windows MinGW's linker links the DLL itself: the host imports python314.dll
# by name, which binds to the copy the loader loaded first by its full path (native/loader/loader.cpp).
target_link_libraries(rackpy_python INTERFACE "${RACKPY_PYTHON_HOME}/${RACKPY_LIBPYTHON}")
