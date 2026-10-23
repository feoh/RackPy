// SPDX-License-Identifier: GPL-3.0-or-later
#include "variants.hpp"

namespace rackpy { namespace widgets {
void bindSvgVariant(py::module_ raw, py::module_ mod) {
	// CPU SVG ownership is needed by SvgWidget. Font/image/NanoVG/GL drawing
	// coverage and the remaining window resources remain RP-27 release gates.
	using S = rack::window::Svg;
	auto window = raw.def_submodule("window", "Partial SVG values used by widgets; window resources remain RP-27.");
	py::class_<S, std::shared_ptr<S>>(window, "Svg")
		.def(py::init([] { runtime::requireMainThread("constructing an SVG"); return std::make_shared<S>(); }))
		.def_static("load", [](const std::string& filename) { runtime::requireMainThread("loading an SVG"); return S::load(filename); }, py::arg("filename"))
		.def("load_file", [](S& svg, const std::string& filename) { runtime::requireMainThread("loading an SVG"); svg.loadFile(filename); }, py::arg("filename"))
		.def("load_string", [](S& svg, const std::string& data) { runtime::requireMainThread("parsing an SVG"); svg.loadString(data); }, py::arg("data"))
		.def("get_size", [](S& svg) { runtime::requireMainThread("reading an SVG"); return svg.getSize(); })
		.def("get_num_shapes", [](S& svg) { runtime::requireMainThread("reading an SVG"); return svg.getNumShapes(); })
		.def("get_num_paths", [](S& svg) { runtime::requireMainThread("reading an SVG"); return svg.getNumPaths(); })
		.def("get_num_points", [](S& svg) { runtime::requireMainThread("reading an SVG"); return svg.getNumPoints(); })
		.def("draw", [](S& svg, const Graphics& graphics) { svg.draw(graphics.get()); }, py::arg("graphics"));
	using V = rack::widget::SvgWidget;
	auto cls = variant<V>(mod, "SvgWidget");
	cls.def_property("svg", [](py::object self) { return native<V>(self).svg; },
		[](py::object self, std::shared_ptr<S> svg) { native<V>(self).svg = std::move(svg); })
		.def("wrap", [](py::object self) { native<V>(self).wrap(); })
		.def("set_svg", [](py::object self, std::shared_ptr<S> svg) { native<V>(self).setSvg(std::move(svg)); }, py::arg("svg"))
		.def("set_svg_legacy", [](py::object self, std::shared_ptr<S> svg) {
			if (PyErr_WarnEx(PyExc_DeprecationWarning, "set_svg_legacy mirrors deprecated setSVG; use set_svg", 1) < 0)
				throw py::error_already_set();
			native<V>(self).setSvg(std::move(svg));
		}, py::arg("svg"));
	mod.attr("SVGWidget") = cls;
}
} }
