// SPDX-License-Identifier: GPL-3.0-or-later
// NVGcolor (nanovg.h, a boundary type: docs/coverage.md policy 13) and
// rack::color (color.hpp) -> rackpy.raw.nanovg.NVGcolor and rackpy.raw.color.
//
// Rack passes NVGcolor by value everywhere, so the Python type is an immutable,
// hashable value: no API needs a live view of one, and the module constants
// cannot be changed by accident.
#include <cmath>
#include <functional>

#include "values.hpp"

namespace rackpy {
namespace values {

namespace {

NVGcolor make(float r, float g, float b, float a) {
	NVGcolor c;
	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

bool sameBits(const NVGcolor& a, const NVGcolor& b) {
	// Python equality must be consistent with hashing; compare the components exactly.
	return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

}  // namespace

void bindNanovgTypes(py::module_ raw) {
	py::module_ nvg = raw.def_submodule("nanovg", "Types from nanovg.h that Rack's API passes around (boundary types).");
	py::class_<NVGcolor>(nvg, "NVGcolor", "An RGBA colour with float components, usually 0 to 1. Immutable.")
		.def(py::init(&make), py::arg("r"), py::arg("g"), py::arg("b"), py::arg("a") = 1.f)
		.def_property_readonly("r", [](const NVGcolor& c) { return c.r; })
		.def_property_readonly("g", [](const NVGcolor& c) { return c.g; })
		.def_property_readonly("b", [](const NVGcolor& c) { return c.b; })
		.def_property_readonly("a", [](const NVGcolor& c) { return c.a; })
		.def_property_readonly("rgba", [](const NVGcolor& c) { return py::make_tuple(c.r, c.g, c.b, c.a); })
		.def("__eq__", [](const NVGcolor& a, const NVGcolor& b) { return sameBits(a, b); }, py::is_operator())
		.def("__ne__", [](const NVGcolor& a, const NVGcolor& b) { return !sameBits(a, b); }, py::is_operator())
		.def("__hash__", [](const NVGcolor& c) { return py::hash(py::make_tuple(c.r, c.g, c.b, c.a)); })
		.def("__repr__", [](const NVGcolor& c) {
			return "NVGcolor(" + floatRepr(c.r) + ", " + floatRepr(c.g) + ", " + floatRepr(c.b) + ", " + floatRepr(c.a) + ")";
		})
		.def("__copy__", [](const NVGcolor& c) { return c; })
		.def("__deepcopy__", [](const NVGcolor& c, py::dict) { return c; }, py::arg("memo"));
}

void bindColor(py::module_ raw) {
	py::module_ m = raw.def_submodule("color", "rack::color: utilities for NVGcolor (color.hpp).");
	namespace color = rack::color;
	m.def("is_equal", &color::isEqual, py::arg("a"), py::arg("b"), "True if all RGBA components are equal.");
	m.def("clamp", &color::clamp, py::arg("a"), "Limits each RGBA component to 0..1.");
	m.def("minus", &color::minus, py::arg("a"), py::arg("b"), "a - b per RGB component; alpha from a.");
	m.def("plus", &color::plus, py::arg("a"), py::arg("b"), "a + b per RGB component; alpha from a.");
	m.def("mult", py::overload_cast<NVGcolor, NVGcolor>(&color::mult), py::arg("a"), py::arg("b"),
		"a * b per RGB component; alpha from a.");
	m.def("mult", py::overload_cast<NVGcolor, float>(&color::mult), py::arg("a"), py::arg("x"),
		"RGB components times x; alpha unchanged.");
	m.def("lerp", &color::lerp, py::arg("a"), py::arg("b"), py::arg("t"), "Interpolates RGBA from a (t = 0) to b (t = 1).");
	m.def("screen", &color::screen, py::arg("a"), py::arg("b"), "Screen blend with alpha compositing.");
	m.def("alpha", &color::alpha, py::arg("a"), py::arg("alpha"), "Alpha times alpha; RGB unchanged.");
	m.def("from_hex_string", &color::fromHexString, py::arg("s"),
		"Parses \"#RRGGBB\" or \"#RRGGBBAA\" as Rack does, with sscanf: it never raises, and returns whatever it "
		"parsed, so unparseable text gives opaque black. (color.hpp says WHITE; Rack's implementation does not.)");
	m.def("to_hex_string", &color::toHexString, py::arg("c"),
		"\"#RRGGBB\" if opaque, else \"#RRGGBBAA\"; components rounded to the nearest 8-bit value.");
	m.attr("BLACK_TRANSPARENT") = color::BLACK_TRANSPARENT;
	m.attr("WHITE_TRANSPARENT") = color::WHITE_TRANSPARENT;
	m.attr("BLACK") = color::BLACK;
	m.attr("RED") = color::RED;
	m.attr("GREEN") = color::GREEN;
	m.attr("BLUE") = color::BLUE;
	m.attr("CYAN") = color::CYAN;
	m.attr("MAGENTA") = color::MAGENTA;
	m.attr("YELLOW") = color::YELLOW;
	m.attr("WHITE") = color::WHITE;
}

}  // namespace values
}  // namespace rackpy
