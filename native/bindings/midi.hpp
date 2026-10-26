// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

namespace rackpy {
namespace bindings {

/** rackpy.raw.midi (RP-29A). */
void bindMidi(pybind11::module_ midi);

}  // namespace bindings
}  // namespace rackpy
