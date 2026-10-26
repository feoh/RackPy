// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

namespace rackpy {
namespace widgets {

/** rackpy.raw.ui (RP-26A). Needs rackpy.raw.widget bound first. */
void bindUi(pybind11::module_ raw);

}  // namespace widgets
}  // namespace rackpy
