// SPDX-License-Identifier: GPL-3.0-or-later
// Part of rackpy.raw.componentlibrary (components.cpp), in its own translation unit to compile in parallel.
#include "components.hpp"

namespace rackpy {
namespace widgets {
namespace appdetail {

void bindComponentSwitches(py::module_ mod) {
	RACKPY_CL_SWITCHES(RACKPY_CL_PARAM)
}

}  // namespace appdetail
}  // namespace widgets
}  // namespace rackpy
