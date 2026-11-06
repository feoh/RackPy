// SPDX-License-Identifier: GPL-3.0-or-later
#include "variants.hpp"
#include "../drawing/drawing.hpp"

#include <cstdint>
#include <unordered_map>

namespace rackpy { namespace widgets {
namespace {
using S = rack::window::Svg;

struct SvgGeneration {
	std::weak_ptr<S> owner;
	std::uint64_t value = 0;
};

std::unordered_map<S*, std::shared_ptr<SvgGeneration>>& svgGenerations() {
	static std::unordered_map<S*, std::shared_ptr<SvgGeneration>> states;
	return states;
}

std::shared_ptr<SvgGeneration> svgGeneration(const std::shared_ptr<S>& svg) {
	auto& states = svgGenerations();
	for (auto it = states.begin(); it != states.end();) {
		if (it->second->owner.expired())
			it = states.erase(it);
		else
			++it;
	}
	auto it = states.find(svg.get());
	if (it != states.end() && it->second->owner.lock() == svg)
		return it->second;
	auto state = std::make_shared<SvgGeneration>();
	state->owner = svg;
	states[svg.get()] = state;
	return state;
}
}  // namespace

void bindSvgVariant(py::module_ raw, py::module_ mod) {
	// SvgWidget needs Svg, so rack::window starts here; bind_window() binds the
	// rest (drawing/resources.cpp).
	auto window = raw.def_submodule("window", "rack::window (bind_window() completes it).");
	drawing::bindNativeHandle(window);
	py::class_<S, std::shared_ptr<S>>(window, "Svg")
		.def(py::init([] { runtime::requireMainThread("constructing an SVG"); return std::make_shared<S>(); }))
		.def_static("load", [](const std::string& filename) { runtime::requireMainThread("loading an SVG"); return S::load(filename); }, py::arg("filename"))
		.def("load_file", [](std::shared_ptr<S> svg, const std::string& filename) {
			runtime::requireMainThread("loading an SVG");
			svg->loadFile(filename);
			++svgGeneration(svg)->value;
		}, py::arg("filename"), "Rack's loadFile: on failure the previous image is cleared.")
		.def("load_string", [](std::shared_ptr<S> svg, const std::string& data) {
			runtime::requireMainThread("parsing an SVG");
			svg->loadString(data);
			++svgGeneration(svg)->value;
		}, py::arg("data"), "Rack's loadString: on failure the previous image is cleared.")
		.def("get_size", [](S& svg) { runtime::requireMainThread("reading an SVG"); return svg.getSize(); })
		.def("get_num_shapes", [](S& svg) { runtime::requireMainThread("reading an SVG"); return svg.getNumShapes(); })
		.def("get_num_paths", [](S& svg) { runtime::requireMainThread("reading an SVG"); return svg.getNumPaths(); })
		.def("get_num_points", [](S& svg) { runtime::requireMainThread("reading an SVG"); return svg.getNumPoints(); })
		.def("draw", [](S& svg, const Graphics& graphics) { svg.draw(graphics.draw()); }, py::arg("graphics"))
		.def_property_readonly("handle", [](std::shared_ptr<S> svg) -> py::object {
			runtime::requireMainThread("reading an SVG");
			NSVGimage* image = svg->handle;
			if (!image) return py::none();
			std::weak_ptr<S> weak = svg;
			auto generation = svgGeneration(svg);
			const auto version = generation->value;
			// Holds the Svg, so the parsed image lives until the Svg replaces it.
			return py::cast(drawing::NativeHandle{"NSVGimage", image, [weak, generation, version, image] {
				auto held = weak.lock();
				return held && generation->value == version && held->handle == image;
			}, svg});
		}, "The parsed NanoSVG image, as an opaque NativeHandle for svg_draw(); None if nothing is loaded.");
	window.def("svg_draw", [](const Graphics& graphics, const drawing::NativeHandle& image) {
		auto* vg = graphics.draw();
		rack::window::svgDraw(vg, static_cast<NSVGimage*>(image.get("NSVGimage")));
	}, py::arg("vg"), py::arg("svg"), "Rack's svgDraw: draws an Svg's handle with the drawing callback's GraphicsContext.");
	using V = rack::widget::SvgWidget;
	auto cls = variant<V>(mod, "SvgWidget");
	cls.def_property("svg", [](py::object self) { return native<V>(self).svg; },
		[](py::object self, std::shared_ptr<S> svg) { native<V>(self).svg = drawableSvg(std::move(svg)); },
		"The raw field: None or a loaded Svg (it does not resize the widget).")
		.def("wrap", [](py::object self) { native<V>(self).wrap(); })
		.def("set_svg", [](py::object self, std::shared_ptr<S> svg) { native<V>(self).setSvg(drawableSvg(std::move(svg))); }, py::arg("svg"))
		.def("set_svg_legacy", [](py::object self, std::shared_ptr<S> svg) {
			if (PyErr_WarnEx(PyExc_DeprecationWarning, "set_svg_legacy mirrors deprecated setSVG; use set_svg", 1) < 0)
				throw py::error_already_set();
			native<V>(self).setSvg(drawableSvg(std::move(svg)));
		}, py::arg("svg"));
}
} }
