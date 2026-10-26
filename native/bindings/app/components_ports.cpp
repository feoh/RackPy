// SPDX-License-Identifier: GPL-3.0-or-later
// Part of rackpy.raw.componentlibrary (components.cpp), in its own translation unit to compile in parallel.
#include "components.hpp"

namespace rackpy {
namespace widgets {
namespace appdetail {

void bindComponentPorts(py::module_ mod) {
#define RACKPY_CL_PORT(T, P) \
	{ auto cls = variant<cl::T, P, PortWidgetT<cl::T>>(mod, #T); guardPortEvents(cls); }
	RACKPY_CL_PORTS(RACKPY_CL_PORT)
#undef RACKPY_CL_PORT
	variant<cl::ScrewSilver, rack::app::SvgScrew>(mod, "ScrewSilver");
	variant<cl::ScrewBlack, rack::app::SvgScrew>(mod, "ScrewBlack");
	variant<cl::ThemedScrew, rack::app::ThemedSvgScrew>(mod, "ThemedScrew");
	using SD = cl::SegmentDisplay;
	variant<SD>(mod, "SegmentDisplay")
		.def_property("lights_len", [](py::object self) { return native<SD>(self).lightsLen; },
			[](py::object self, int v) { native<SD>(self).lightsLen = v; })
		.def_property("vertical", [](py::object self) { return native<SD>(self).vertical; },
			[](py::object self, bool v) { native<SD>(self).vertical = v; })
		.def_property("margin", [](py::object self) { return native<SD>(self).margin; },
			[](py::object self, float v) { native<SD>(self).margin = v; });
}

}  // namespace appdetail
}  // namespace widgets
}  // namespace rackpy
