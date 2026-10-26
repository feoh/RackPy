// SPDX-License-Identifier: GPL-3.0-or-later
// Part of rackpy.raw.componentlibrary (components.cpp), in its own translation unit to compile in parallel.
#include "components.hpp"

namespace rackpy {
namespace widgets {
namespace appdetail {

void bindComponentKnobs(py::module_ mod) {
	// The knobs with their own SvgWidget children (bg, and Rogan's fg) come first: others derive from them.
#define RACKPY_CL_BG(T) \
	auto T##Class = variant<cl::T, rack::app::SvgKnob, ParamWidgetT<cl::T>>(mod, #T); \
	paramMethods(T##Class); \
	svgChild(T##Class, "bg", &cl::T::bg);
	RACKPY_CL_BG_KNOBS(RACKPY_CL_BG)
#undef RACKPY_CL_BG
	svgChild(RoganClass, "fg", &cl::Rogan::fg);
	RACKPY_CL_KNOBS(RACKPY_CL_PARAM)
}

}  // namespace appdetail
}  // namespace widgets
}  // namespace rackpy
