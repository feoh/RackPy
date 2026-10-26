// SPDX-License-Identifier: GPL-3.0-or-later
// Typed views of borrowed Rack-owned widgets (RP-28A). Rack's own widgets keep
// internal children in public fields (SvgKnob::fb, ::sw, ::tw, ::shadow ...);
// these views reach their public fields, checked and nonowning, like HostWidget.
#include "variants.hpp"
#include <pybind11/stl.h>
#include <array>
#include <cmath>

namespace rackpy { namespace widgets {
namespace {
struct HostFramebuffer : ExternalWidget {};
struct HostSvg : ExternalWidget {};
struct HostTransform : ExternalWidget {};
using F = rack::widget::FramebufferWidget;
using S = rack::widget::SvgWidget;
using T = rack::widget::TransformWidget;
}  // namespace

void bindHostViews(py::module_ mod) {
	py::class_<HostFramebuffer, ExternalWidget>(mod, "HostFramebufferWidget", "A Rack-owned FramebufferWidget's public fields.")
		.def_property("dirty", [](const HostFramebuffer& h) { return hostAs<F>(h).dirty; },
			[](const HostFramebuffer& h, bool v) { hostAs<F>(h).dirty = v; })
		.def_property("bypassed", [](const HostFramebuffer& h) { return hostAs<F>(h).bypassed; },
			[](const HostFramebuffer& h, bool v) { hostAs<F>(h).bypassed = v; })
		.def_property("oversample", [](const HostFramebuffer& h) { return hostAs<F>(h).oversample; },
			[](const HostFramebuffer& h, float v) {
				// Rack renders this framebuffer itself, so check here what a render would allocate.
				if (!std::isfinite(v) || v <= 0.f || v > 16.f)
					throw std::invalid_argument("a host framebuffer's oversample must be finite, above 0 and at most 16");
				hostAs<F>(h).oversample = v;
			}, "Finite, above 0 and at most 16: Rack renders this framebuffer itself, unchecked.")
		.def_property("dirty_on_subpixel_change", [](const HostFramebuffer& h) { return hostAs<F>(h).dirtyOnSubpixelChange; },
			[](const HostFramebuffer& h, bool v) { hostAs<F>(h).dirtyOnSubpixelChange = v; })
		.def("set_dirty", [](const HostFramebuffer& h, bool dirty) { hostAs<F>(h).setDirty(dirty); }, py::arg("dirty") = true)
		.def("get_framebuffer_size", [](const HostFramebuffer& h) { return hostAs<F>(h).getFramebufferSize(); })
		.def("get_image_handle", [](const HostFramebuffer& h) { return hostAs<F>(h).getImageHandle(); });
	registerHostView([](W* w) { return dynamic_cast<F*>(w) != nullptr; },
		[](const ExternalWidget& e) { return py::cast(HostFramebuffer{e}); });

	py::class_<HostSvg, ExternalWidget>(mod, "HostSvgWidget", "A Rack-owned SvgWidget's public fields.")
		.def_property("svg", [](const HostSvg& h) { return hostAs<S>(h).svg; },
			[](const HostSvg& h, std::shared_ptr<rack::window::Svg> svg) { hostAs<S>(h).svg = drawableSvg(std::move(svg)); },
			"The raw field: None or a loaded Svg (it does not resize the widget).")
		.def("set_svg", [](const HostSvg& h, std::shared_ptr<rack::window::Svg> svg) { hostAs<S>(h).setSvg(drawableSvg(std::move(svg))); },
			py::arg("svg"))
		.def("wrap", [](const HostSvg& h) { hostAs<S>(h).wrap(); });
	registerHostView([](W* w) { return dynamic_cast<S*>(w) != nullptr; }, [](const ExternalWidget& e) { return py::cast(HostSvg{e}); });

	py::class_<HostTransform, ExternalWidget>(mod, "HostTransformWidget", "A Rack-owned TransformWidget's public fields.")
		.def_property("transform", [](const HostTransform& h) {
				auto& w = hostAs<T>(h);
				return py::make_tuple(w.transform[0], w.transform[1], w.transform[2], w.transform[3], w.transform[4], w.transform[5]);
			},
			[](const HostTransform& h, std::array<float, 6> value) { std::copy(value.begin(), value.end(), hostAs<T>(h).transform); },
			"A copy, as a 6-tuple; assign six floats to change it.")
		.def("identity", [](const HostTransform& h) { hostAs<T>(h).identity(); })
		.def("translate", [](const HostTransform& h, rack::math::Vec delta) { hostAs<T>(h).translate(delta); }, py::arg("delta"))
		.def("scale", [](const HostTransform& h, rack::math::Vec scale) { hostAs<T>(h).scale(scale); }, py::arg("scale"))
		.def("rotate", [](const HostTransform& h, float angle) { hostAs<T>(h).rotate(angle); }, py::arg("angle"))
		.def("rotate", [](const HostTransform& h, float angle, rack::math::Vec origin) { hostAs<T>(h).rotate(angle, origin); },
			py::arg("angle"), py::arg("origin"));
	registerHostView([](W* w) { return dynamic_cast<T*>(w) != nullptr; },
		[](const ExternalWidget& e) { return py::cast(HostTransform{e}); });
}
} }
