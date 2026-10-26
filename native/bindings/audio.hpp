// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

namespace rackpy {
namespace bindings {

/** rackpy.raw.audio (RP-29B). */
void bindAudio(pybind11::module_ audio);

}  // namespace bindings
}  // namespace rackpy
