// SPDX-License-Identifier: GPL-3.0-or-later
// Test hooks for the runtime, compiled only with -DRACKPY_TEST_HOOKS=ON. They
// add a "RuntimeProbe" module and `_rackpy_host._testing`, so tests can drive
// the runtime inside a real Rack. Production builds contain none of this.
#pragma once
#include <pybind11/pybind11.h>

#include <rack.hpp>

namespace rackpy {
namespace testing {

void define(pybind11::module_ m);
void registerModels(rack::plugin::Plugin* plugin);

}  // namespace testing
}  // namespace rackpy
