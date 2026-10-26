// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

namespace rackpy {
namespace widgets {

/** rackpy.raw.app (RP-28A). Needs rackpy.raw.widget and rackpy.raw.ui bound first. */
void bindApp(pybind11::module_ raw);

}  // namespace widgets
}  // namespace rackpy
