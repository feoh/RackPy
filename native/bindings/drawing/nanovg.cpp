// SPDX-License-Identifier: GPL-3.0-or-later
// NanoVG's drawing API (nanovg.h), the boundary needed to use Rack's DrawArgs
// (docs/coverage.md policy 13), as rackpy.raw.nanovg functions. Every drawing
// function takes the GraphicsContext of the drawing callback it runs in and
// refuses one whose callback has ended or encloses the running one. Rack owns
// the frame (nvgBeginFrame/nvgEndFrame/nvgCancelFrame), the contexts and resource
// creation (Window::loadFont/loadImage), so those functions are not here.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <rack.hpp>

#include "drawing.hpp"
#include "../widget/events.hpp"

namespace rackpy { namespace drawing {
namespace py = pybind11;
using widgets::Graphics;

namespace {
using Xform = std::array<float, 6>;

/** Python tuples, as the stubs promise (pybind11 makes lists of std::array). */
template<size_t N> py::tuple tuple(const std::array<float, N>& values) {
	py::tuple out(N);
	for (size_t i = 0; i < N; ++i) out[i] = values[i];
	return out;
}

float finite(float value, const char* what) {
	if (!std::isfinite(value)) throw std::invalid_argument(std::string(what) + " must be finite");
	return value;
}

/** Character index of every UTF-8 byte offset of a string (one past the end included). */
std::vector<size_t> characterIndices(const std::string& text) {
	std::vector<size_t> index(text.size() + 1);
	size_t character = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80 && i > 0) ++character;
		index[i] = character;
	}
	// A string's end is one character past its last one.
	index[text.size()] = text.empty() ? 0 : character + 1;
	return index;
}

/** An image argument: a handle number, or a window::Image (whose handle is checked). */
int imageHandle(py::handle image) {
	if (py::isinstance<py::int_>(image)) return image.cast<int>();
	return image.attr("handle").cast<int>();
}
/** A font argument: a handle number, or a window::Font. */
int fontHandle(py::handle font) {
	if (py::isinstance<py::int_>(font)) return font.cast<int>();
	return font.attr("handle").cast<int>();  // a live Font's handle (noted as known)
}
/** A font for fontstash's unchecked fallback functions: one Rack has loaded. */
int knownFontHandle(py::handle font) {
	int handle = fontHandle(font);
	if (!knownFont(handle))
		throw std::invalid_argument("font " + std::to_string(handle) + " is not a font loaded in this graphics context "
			"(use a window.Font, or a handle from one or from find_font)");
	return handle;
}

struct GlyphPosition {
	size_t index;
	float x, minx, maxx;
};
struct TextRow {
	size_t start, end, next;
	float width, minx, maxx;
};
}  // namespace

void bindNanovg(py::module_ raw) {
	py::module_ m = raw.attr("nanovg").cast<py::module_>();
	m.doc() = "NanoVG (nanovg.h): the drawing API for Rack's DrawArgs. Drawing functions take the running drawing "
		"callback's GraphicsContext; Rack owns frames, contexts and resource creation.";

	// Constants keep NanoVG's names.
	m.attr("NVG_PI") = NVG_PI;
	m.attr("NVG_CCW") = int(NVG_CCW);
	m.attr("NVG_CW") = int(NVG_CW);
	m.attr("NVG_SOLID") = int(NVG_SOLID);
	m.attr("NVG_HOLE") = int(NVG_HOLE);
	m.attr("NVG_BUTT") = int(NVG_BUTT);
	m.attr("NVG_ROUND") = int(NVG_ROUND);
	m.attr("NVG_SQUARE") = int(NVG_SQUARE);
	m.attr("NVG_BEVEL") = int(NVG_BEVEL);
	m.attr("NVG_MITER") = int(NVG_MITER);
	m.attr("NVG_ALIGN_LEFT") = int(NVG_ALIGN_LEFT);
	m.attr("NVG_ALIGN_CENTER") = int(NVG_ALIGN_CENTER);
	m.attr("NVG_ALIGN_RIGHT") = int(NVG_ALIGN_RIGHT);
	m.attr("NVG_ALIGN_TOP") = int(NVG_ALIGN_TOP);
	m.attr("NVG_ALIGN_MIDDLE") = int(NVG_ALIGN_MIDDLE);
	m.attr("NVG_ALIGN_BOTTOM") = int(NVG_ALIGN_BOTTOM);
	m.attr("NVG_ALIGN_BASELINE") = int(NVG_ALIGN_BASELINE);
	m.attr("NVG_ZERO") = int(NVG_ZERO);
	m.attr("NVG_ONE") = int(NVG_ONE);
	m.attr("NVG_SRC_COLOR") = int(NVG_SRC_COLOR);
	m.attr("NVG_ONE_MINUS_SRC_COLOR") = int(NVG_ONE_MINUS_SRC_COLOR);
	m.attr("NVG_DST_COLOR") = int(NVG_DST_COLOR);
	m.attr("NVG_ONE_MINUS_DST_COLOR") = int(NVG_ONE_MINUS_DST_COLOR);
	m.attr("NVG_SRC_ALPHA") = int(NVG_SRC_ALPHA);
	m.attr("NVG_ONE_MINUS_SRC_ALPHA") = int(NVG_ONE_MINUS_SRC_ALPHA);
	m.attr("NVG_DST_ALPHA") = int(NVG_DST_ALPHA);
	m.attr("NVG_ONE_MINUS_DST_ALPHA") = int(NVG_ONE_MINUS_DST_ALPHA);
	m.attr("NVG_SRC_ALPHA_SATURATE") = int(NVG_SRC_ALPHA_SATURATE);
	m.attr("NVG_SOURCE_OVER") = int(NVG_SOURCE_OVER);
	m.attr("NVG_SOURCE_IN") = int(NVG_SOURCE_IN);
	m.attr("NVG_SOURCE_OUT") = int(NVG_SOURCE_OUT);
	m.attr("NVG_ATOP") = int(NVG_ATOP);
	m.attr("NVG_DESTINATION_OVER") = int(NVG_DESTINATION_OVER);
	m.attr("NVG_DESTINATION_IN") = int(NVG_DESTINATION_IN);
	m.attr("NVG_DESTINATION_OUT") = int(NVG_DESTINATION_OUT);
	m.attr("NVG_DESTINATION_ATOP") = int(NVG_DESTINATION_ATOP);
	m.attr("NVG_LIGHTER") = int(NVG_LIGHTER);
	m.attr("NVG_COPY") = int(NVG_COPY);
	m.attr("NVG_XOR") = int(NVG_XOR);
	m.attr("NVG_IMAGE_GENERATE_MIPMAPS") = int(NVG_IMAGE_GENERATE_MIPMAPS);
	m.attr("NVG_IMAGE_REPEATX") = int(NVG_IMAGE_REPEATX);
	m.attr("NVG_IMAGE_REPEATY") = int(NVG_IMAGE_REPEATY);
	m.attr("NVG_IMAGE_FLIPY") = int(NVG_IMAGE_FLIPY);
	m.attr("NVG_IMAGE_PREMULTIPLIED") = int(NVG_IMAGE_PREMULTIPLIED);
	m.attr("NVG_IMAGE_NEAREST") = int(NVG_IMAGE_NEAREST);

	py::class_<NVGpaint>(m, "NVGpaint", "A gradient or image paint, from the gradient and pattern functions. Immutable.")
		.def_property_readonly("xform", [](const NVGpaint& p) { return tuple(Xform{p.xform[0], p.xform[1], p.xform[2], p.xform[3], p.xform[4], p.xform[5]}); })
		.def_property_readonly("extent", [](const NVGpaint& p) { return tuple(std::array<float, 2>{p.extent[0], p.extent[1]}); })
		.def_property_readonly("radius", [](const NVGpaint& p) { return p.radius; })
		.def_property_readonly("feather", [](const NVGpaint& p) { return p.feather; })
		.def_property_readonly("inner_color", [](const NVGpaint& p) { return p.innerColor; })
		.def_property_readonly("outer_color", [](const NVGpaint& p) { return p.outerColor; })
		.def_property_readonly("image", [](const NVGpaint& p) { return p.image; })
		.def("__repr__", [](const NVGpaint& p) { return "<NVGpaint image=" + std::to_string(p.image) + ">"; });
	py::class_<GlyphPosition>(m, "NVGglyphPosition", "A glyph's position from text_glyph_positions(); index is a character index.")
		.def_readonly("index", &GlyphPosition::index)
		.def_readonly("x", &GlyphPosition::x)
		.def_readonly("minx", &GlyphPosition::minx)
		.def_readonly("maxx", &GlyphPosition::maxx);
	py::class_<TextRow>(m, "NVGtextRow", "A line from text_break_lines(); start, end and next are character indices.")
		.def_readonly("start", &TextRow::start)
		.def_readonly("end", &TextRow::end)
		.def_readonly("next", &TextRow::next)
		.def_readonly("width", &TextRow::width)
		.def_readonly("minx", &TextRow::minx)
		.def_readonly("maxx", &TextRow::maxx);

	auto vg = py::arg("vg");
	// Composite operation
	m.def("global_composite_operation", [](const Graphics& g, int op) { nvgGlobalCompositeOperation(g.draw(), op); }, vg, py::arg("op"));
	m.def("global_composite_blend_func", [](const Graphics& g, int s, int d) { nvgGlobalCompositeBlendFunc(g.draw(), s, d); },
		vg, py::arg("sfactor"), py::arg("dfactor"));
	m.def("global_composite_blend_func_separate", [](const Graphics& g, int a, int b, int c, int d) {
		nvgGlobalCompositeBlendFuncSeparate(g.draw(), a, b, c, d);
	}, vg, py::arg("src_rgb"), py::arg("dst_rgb"), py::arg("src_alpha"), py::arg("dst_alpha"));

	// Colours: pure values, usable anywhere
	m.def("rgb", &nvgRGB, py::arg("r"), py::arg("g"), py::arg("b"), "An opaque colour from 0-255 components.");
	m.def("rgbf", &nvgRGBf, py::arg("r"), py::arg("g"), py::arg("b"), "An opaque colour from 0-1 components.");
	m.def("rgba", &nvgRGBA, py::arg("r"), py::arg("g"), py::arg("b"), py::arg("a"), "A colour from 0-255 components.");
	m.def("rgbaf", &nvgRGBAf, py::arg("r"), py::arg("g"), py::arg("b"), py::arg("a"), "A colour from 0-1 components.");
	m.def("lerp_rgba", &nvgLerpRGBA, py::arg("c0"), py::arg("c1"), py::arg("u"), "Interpolates from c0 (u = 0) to c1 (u = 1).");
	m.def("trans_rgba", &nvgTransRGBA, py::arg("c0"), py::arg("a"), "c0 with alpha a (0-255).");
	m.def("trans_rgbaf", &nvgTransRGBAf, py::arg("c0"), py::arg("a"), "c0 with alpha a (0-1).");
	m.def("hsl", &nvgHSL, py::arg("h"), py::arg("s"), py::arg("l"), "An opaque colour from hue, saturation and lightness (0-1).");
	m.def("hsla", &nvgHSLA, py::arg("h"), py::arg("s"), py::arg("l"), py::arg("a"), "hsl() with alpha a (0-255).");

	// State
	m.def("save", [](const Graphics& g) {
		auto& state = g.state();
		nvgSave(state.vg);
		++state.saves;
	}, vg, "Pushes the render state; the callback's unrestored saves are restored when it ends.");
	m.def("restore", [](const Graphics& g) {
		auto& state = g.state();
		if (state.saves == 0)
			throw runtime::Ownership("restore() without a matching save() in this drawing callback would pop Rack's state");
		nvgRestore(state.vg);
		--state.saves;
	}, vg);
	m.def("reset", [](const Graphics& g) { nvgReset(g.draw()); }, vg, "Resets the current render state to defaults; the state stack is unchanged.");

	// Render styles
	m.def("shape_anti_alias", [](const Graphics& g, bool enabled) { nvgShapeAntiAlias(g.draw(), enabled); }, vg, py::arg("enabled"));
	m.def("stroke_color", [](const Graphics& g, NVGcolor c) { nvgStrokeColor(g.draw(), c); }, vg, py::arg("color"));
	m.def("stroke_paint", [](const Graphics& g, NVGpaint p) { nvgStrokePaint(g.draw(), p); }, vg, py::arg("paint"));
	m.def("fill_color", [](const Graphics& g, NVGcolor c) { nvgFillColor(g.draw(), c); }, vg, py::arg("color"));
	m.def("fill_paint", [](const Graphics& g, NVGpaint p) { nvgFillPaint(g.draw(), p); }, vg, py::arg("paint"));
	m.def("miter_limit", [](const Graphics& g, float limit) { nvgMiterLimit(g.draw(), limit); }, vg, py::arg("limit"));
	m.def("stroke_width", [](const Graphics& g, float size) { nvgStrokeWidth(g.draw(), size); }, vg, py::arg("size"));
	m.def("line_cap", [](const Graphics& g, int cap) { nvgLineCap(g.draw(), cap); }, vg, py::arg("cap"));
	m.def("line_join", [](const Graphics& g, int join) { nvgLineJoin(g.draw(), join); }, vg, py::arg("join"));
	m.def("global_alpha", [](const Graphics& g, float alpha) { nvgGlobalAlpha(g.draw(), alpha); }, vg, py::arg("alpha"));
	m.def("global_tint", [](const Graphics& g, NVGcolor tint) { nvgGlobalTint(g.draw(), tint); }, vg, py::arg("tint"),
		"Rack's NanoVG: multiplies every colour drawn (Rack dims panels with it).");
	m.def("get_global_tint", [](const Graphics& g) { return nvgGetGlobalTint(g.draw()); }, vg);
	m.def("alpha", [](const Graphics& g, float alpha) { nvgAlpha(g.draw(), alpha); }, vg, py::arg("alpha"),
		"Rack's NanoVG: multiplies the global alpha by alpha.");
	m.def("tint", [](const Graphics& g, NVGcolor tint) { nvgTint(g.draw(), tint); }, vg, py::arg("tint"),
		"Rack's NanoVG: multiplies the global tint by tint.");

	// Transforms
	m.def("reset_transform", [](const Graphics& g) { nvgResetTransform(g.draw()); }, vg);
	m.def("transform", [](const Graphics& g, float a, float b, float c, float d, float e, float f) {
		nvgTransform(g.draw(), a, b, c, d, e, f);
	}, vg, py::arg("a"), py::arg("b"), py::arg("c"), py::arg("d"), py::arg("e"), py::arg("f"));
	m.def("translate", [](const Graphics& g, float x, float y) { nvgTranslate(g.draw(), x, y); }, vg, py::arg("x"), py::arg("y"));
	m.def("rotate", [](const Graphics& g, float angle) { nvgRotate(g.draw(), angle); }, vg, py::arg("angle"));
	m.def("skew_x", [](const Graphics& g, float angle) { nvgSkewX(g.draw(), angle); }, vg, py::arg("angle"));
	m.def("skew_y", [](const Graphics& g, float angle) { nvgSkewY(g.draw(), angle); }, vg, py::arg("angle"));
	m.def("scale", [](const Graphics& g, float x, float y) { nvgScale(g.draw(), x, y); }, vg, py::arg("x"), py::arg("y"));
	m.def("current_transform", [](const Graphics& g) { Xform t; nvgCurrentTransform(g.draw(), t.data()); return tuple(t); }, vg,
		"The current transform as (a, b, c, d, e, f).");
	m.def("transform_identity", [] { Xform t; nvgTransformIdentity(t.data()); return tuple(t); });
	m.def("transform_translate", [](float tx, float ty) { Xform t; nvgTransformTranslate(t.data(), tx, ty); return tuple(t); },
		py::arg("tx"), py::arg("ty"));
	m.def("transform_scale", [](float sx, float sy) { Xform t; nvgTransformScale(t.data(), sx, sy); return tuple(t); },
		py::arg("sx"), py::arg("sy"));
	m.def("transform_rotate", [](float a) { Xform t; nvgTransformRotate(t.data(), a); return tuple(t); }, py::arg("a"));
	m.def("transform_skew_x", [](float a) { Xform t; nvgTransformSkewX(t.data(), a); return tuple(t); }, py::arg("a"));
	m.def("transform_skew_y", [](float a) { Xform t; nvgTransformSkewY(t.data(), a); return tuple(t); }, py::arg("a"));
	m.def("transform_multiply", [](Xform dst, const Xform& src) { nvgTransformMultiply(dst.data(), src.data()); return tuple(dst); },
		py::arg("dst"), py::arg("src"), "dst * src: dst followed by src, as nvgTransformMultiply leaves in dst.");
	m.def("transform_premultiply", [](Xform dst, const Xform& src) { nvgTransformPremultiply(dst.data(), src.data()); return tuple(dst); },
		py::arg("dst"), py::arg("src"), "src * dst, as nvgTransformPremultiply leaves in dst.");
	m.def("transform_inverse", [](const Xform& src) -> py::object {
		Xform dst;
		if (!nvgTransformInverse(dst.data(), src.data())) return py::none();
		return tuple(dst);
	}, py::arg("src"), "The inverse transform, or None if src is not invertible.");
	m.def("transform_point", [](const Xform& xform, float x, float y) {
		float dx, dy;
		nvgTransformPoint(&dx, &dy, xform.data(), x, y);
		return std::make_pair(dx, dy);
	}, py::arg("xform"), py::arg("x"), py::arg("y"));
	m.def("deg_to_rad", &nvgDegToRad, py::arg("deg"));
	m.def("rad_to_deg", &nvgRadToDeg, py::arg("rad"));

	// Images (Rack creates and deletes them: Window.load_image, FramebufferWidget)
	m.def("image_size", [](const Graphics& g, py::handle image) {
		int w = 0, h = 0;
		nvgImageSize(g.draw(), imageHandle(image), &w, &h);
		return std::make_pair(w, h);
	}, vg, py::arg("image"), "(width, height) of an image handle or window.Image; (0, 0) for an unknown handle.");

	// Paints
	m.def("linear_gradient", [](const Graphics& g, float sx, float sy, float ex, float ey, NVGcolor icol, NVGcolor ocol) {
		return nvgLinearGradient(g.draw(), sx, sy, ex, ey, icol, ocol);
	}, vg, py::arg("sx"), py::arg("sy"), py::arg("ex"), py::arg("ey"), py::arg("icol"), py::arg("ocol"));
	m.def("box_gradient", [](const Graphics& g, float x, float y, float w, float h, float r, float f, NVGcolor icol, NVGcolor ocol) {
		return nvgBoxGradient(g.draw(), x, y, w, h, r, f, icol, ocol);
	}, vg, py::arg("x"), py::arg("y"), py::arg("w"), py::arg("h"), py::arg("r"), py::arg("f"), py::arg("icol"), py::arg("ocol"));
	m.def("radial_gradient", [](const Graphics& g, float cx, float cy, float inr, float outr, NVGcolor icol, NVGcolor ocol) {
		return nvgRadialGradient(g.draw(), cx, cy, inr, outr, icol, ocol);
	}, vg, py::arg("cx"), py::arg("cy"), py::arg("inr"), py::arg("outr"), py::arg("icol"), py::arg("ocol"));
	m.def("image_pattern", [](const Graphics& g, float ox, float oy, float ex, float ey, float angle, py::handle image, float alpha) {
		return nvgImagePattern(g.draw(), ox, oy, ex, ey, angle, imageHandle(image), alpha);
	}, vg, py::arg("ox"), py::arg("oy"), py::arg("ex"), py::arg("ey"), py::arg("angle"), py::arg("image"), py::arg("alpha"),
		"A pattern of an image handle or window.Image; an unknown handle paints nothing.");

	// Scissoring
	m.def("scissor", [](const Graphics& g, float x, float y, float w, float h) { nvgScissor(g.draw(), x, y, w, h); },
		vg, py::arg("x"), py::arg("y"), py::arg("w"), py::arg("h"));
	m.def("intersect_scissor", [](const Graphics& g, float x, float y, float w, float h) { nvgIntersectScissor(g.draw(), x, y, w, h); },
		vg, py::arg("x"), py::arg("y"), py::arg("w"), py::arg("h"));
	m.def("reset_scissor", [](const Graphics& g) { nvgResetScissor(g.draw()); }, vg);

	// Paths
	m.def("begin_path", [](const Graphics& g) { nvgBeginPath(g.draw()); }, vg);
	m.def("move_to", [](const Graphics& g, float x, float y) { nvgMoveTo(g.draw(), x, y); }, vg, py::arg("x"), py::arg("y"));
	m.def("line_to", [](const Graphics& g, float x, float y) { nvgLineTo(g.draw(), x, y); }, vg, py::arg("x"), py::arg("y"));
	m.def("bezier_to", [](const Graphics& g, float c1x, float c1y, float c2x, float c2y, float x, float y) {
		nvgBezierTo(g.draw(), c1x, c1y, c2x, c2y, x, y);
	}, vg, py::arg("c1x"), py::arg("c1y"), py::arg("c2x"), py::arg("c2y"), py::arg("x"), py::arg("y"));
	m.def("quad_to", [](const Graphics& g, float cx, float cy, float x, float y) { nvgQuadTo(g.draw(), cx, cy, x, y); },
		vg, py::arg("cx"), py::arg("cy"), py::arg("x"), py::arg("y"));
	m.def("arc_to", [](const Graphics& g, float x1, float y1, float x2, float y2, float radius) {
		nvgArcTo(g.draw(), x1, y1, x2, y2, radius);
	}, vg, py::arg("x1"), py::arg("y1"), py::arg("x2"), py::arg("y2"), py::arg("radius"));
	m.def("close_path", [](const Graphics& g) { nvgClosePath(g.draw()); }, vg);
	m.def("path_winding", [](const Graphics& g, int dir) { nvgPathWinding(g.draw(), dir); }, vg, py::arg("dir"));
	m.def("arc", [](const Graphics& g, float cx, float cy, float r, float a0, float a1, int dir) {
		nvgArc(g.draw(), cx, cy, r, a0, a1, dir);
	}, vg, py::arg("cx"), py::arg("cy"), py::arg("r"), py::arg("a0"), py::arg("a1"), py::arg("dir"));
	m.def("rect", [](const Graphics& g, float x, float y, float w, float h) { nvgRect(g.draw(), x, y, w, h); },
		vg, py::arg("x"), py::arg("y"), py::arg("w"), py::arg("h"));
	m.def("rounded_rect", [](const Graphics& g, float x, float y, float w, float h, float r) { nvgRoundedRect(g.draw(), x, y, w, h, r); },
		vg, py::arg("x"), py::arg("y"), py::arg("w"), py::arg("h"), py::arg("r"));
	m.def("rounded_rect_varying", [](const Graphics& g, float x, float y, float w, float h, float tl, float tr, float br, float bl) {
		nvgRoundedRectVarying(g.draw(), x, y, w, h, tl, tr, br, bl);
	}, vg, py::arg("x"), py::arg("y"), py::arg("w"), py::arg("h"), py::arg("rad_top_left"), py::arg("rad_top_right"),
		py::arg("rad_bottom_right"), py::arg("rad_bottom_left"));
	m.def("ellipse", [](const Graphics& g, float cx, float cy, float rx, float ry) { nvgEllipse(g.draw(), cx, cy, rx, ry); },
		vg, py::arg("cx"), py::arg("cy"), py::arg("rx"), py::arg("ry"));
	m.def("circle", [](const Graphics& g, float cx, float cy, float r) { nvgCircle(g.draw(), cx, cy, r); },
		vg, py::arg("cx"), py::arg("cy"), py::arg("r"));
	m.def("fill", [](const Graphics& g) { nvgFill(g.draw()); }, vg);
	m.def("stroke", [](const Graphics& g) { nvgStroke(g.draw()); }, vg);

	// Fonts (Rack loads them: Window.load_font) and text
	m.def("find_font", [](const Graphics& g, const std::string& name) {
		int handle = nvgFindFont(g.draw(), name.c_str());
		noteFont(handle);
		return handle;
	},
		vg, py::arg("name"), "A loaded font's handle by name, or -1.");
	m.def("add_fallback_font_id", [](const Graphics& g, py::handle base, py::handle fallback) {
		auto* ctx = g.draw();
		return nvgAddFallbackFontId(ctx, knownFontHandle(base), knownFontHandle(fallback));
	}, vg, py::arg("base_font"), py::arg("fallback_font"), "Takes loaded fonts: window.Font objects or their handles.");
	m.def("add_fallback_font", [](const Graphics& g, const std::string& base, const std::string& fallback) {
		return nvgAddFallbackFont(g.draw(), base.c_str(), fallback.c_str());
	}, vg, py::arg("base_font"), py::arg("fallback_font"));
	m.def("reset_fallback_fonts_id", [](const Graphics& g, py::handle base) {
		auto* ctx = g.draw();
		nvgResetFallbackFontsId(ctx, knownFontHandle(base));
	}, vg, py::arg("base_font"));
	m.def("reset_fallback_fonts", [](const Graphics& g, const std::string& base) {
		auto* ctx = g.draw();
		if (nvgFindFont(ctx, base.c_str()) < 0) throw std::invalid_argument("no font named " + base);
		nvgResetFallbackFonts(ctx, base.c_str());
	}, vg, py::arg("base_font"), "Raises ValueError for an unknown name (NanoVG would crash).");
	m.def("font_size", [](const Graphics& g, float size) {
		// fontstash keeps sizes as tenths in a short.
		if (!std::isfinite(size) || size < 0.f || size > 3276.f)
			throw std::invalid_argument("font size must be finite and from 0 to 3276 (NanoVG's limit)");
		nvgFontSize(g.draw(), size);
	}, vg, py::arg("size"));
	m.def("font_blur", [](const Graphics& g, float blur) { nvgFontBlur(g.draw(), finite(blur, "font blur")); }, vg, py::arg("blur"));
	m.def("text_letter_spacing", [](const Graphics& g, float spacing) { nvgTextLetterSpacing(g.draw(), finite(spacing, "letter spacing")); },
		vg, py::arg("spacing"));
	m.def("text_line_height", [](const Graphics& g, float height) { nvgTextLineHeight(g.draw(), finite(height, "line height")); },
		vg, py::arg("line_height"));
	m.def("text_align", [](const Graphics& g, int align) { nvgTextAlign(g.draw(), align); }, vg, py::arg("align"));
	m.def("font_face_id", [](const Graphics& g, py::handle font) { nvgFontFaceId(g.draw(), fontHandle(font)); },
		vg, py::arg("font"), "Selects a font by handle or window.Font.");
	m.def("font_face", [](const Graphics& g, const std::string& name) { nvgFontFace(g.draw(), name.c_str()); }, vg, py::arg("font"));
	m.def("text", [](const Graphics& g, float x, float y, const std::string& s) {
		return nvgText(g.draw(), x, y, s.data(), s.data() + s.size());
	}, vg, py::arg("x"), py::arg("y"), py::arg("string"), "Draws text; returns the x after it.");
	m.def("text_box", [](const Graphics& g, float x, float y, float width, const std::string& s) {
		nvgTextBox(g.draw(), x, y, width, s.data(), s.data() + s.size());
	}, vg, py::arg("x"), py::arg("y"), py::arg("break_row_width"), py::arg("string"));
	m.def("text_bounds", [](const Graphics& g, float x, float y, const std::string& s) {
		std::array<float, 4> bounds{};
		float advance = nvgTextBounds(g.draw(), x, y, s.data(), s.data() + s.size(), bounds.data());
		return py::make_tuple(advance, tuple(bounds));
	}, vg, py::arg("x"), py::arg("y"), py::arg("string"), "(advance, (xmin, ymin, xmax, ymax)).");
	m.def("text_box_bounds", [](const Graphics& g, float x, float y, float width, const std::string& s) {
		std::array<float, 4> bounds{};
		nvgTextBoxBounds(g.draw(), x, y, width, s.data(), s.data() + s.size(), bounds.data());
		return tuple(bounds);
	}, vg, py::arg("x"), py::arg("y"), py::arg("break_row_width"), py::arg("string"), "(xmin, ymin, xmax, ymax).");
	m.def("text_glyph_positions", [](const Graphics& g, float x, float y, const std::string& s) {
		auto* ctx = g.draw();
		auto index = characterIndices(s);
		std::vector<NVGglyphPosition> positions(s.size());
		int n = s.empty() ? 0 : nvgTextGlyphPositions(ctx, x, y, s.data(), s.data() + s.size(), positions.data(), int(positions.size()));
		std::vector<GlyphPosition> out;
		for (int i = 0; i < n; ++i)
			out.push_back({index[size_t(positions[i].str - s.data())], positions[i].x, positions[i].minx, positions[i].maxx});
		return out;
	}, vg, py::arg("x"), py::arg("y"), py::arg("string"));
	m.def("text_metrics", [](const Graphics& g) {
		float ascender = 0.f, descender = 0.f, lineh = 0.f;
		nvgTextMetrics(g.draw(), &ascender, &descender, &lineh);
		return std::make_tuple(ascender, descender, lineh);
	}, vg, "(ascender, descender, line height) of the current font; zeros without one.");
	m.def("text_break_lines", [](const Graphics& g, const std::string& s, float width) {
		auto* ctx = g.draw();
		auto index = characterIndices(s);
		std::vector<TextRow> out;
		const char* start = s.data();
		const char* end = s.data() + s.size();
		NVGtextRow rows[32];
		while (start < end) {
			int n = nvgTextBreakLines(ctx, start, end, width, rows, 32);
			if (n <= 0) break;
			for (int i = 0; i < n; ++i)
				out.push_back({index[size_t(rows[i].start - s.data())], index[size_t(rows[i].end - s.data())],
					index[size_t(rows[i].next - s.data())], rows[i].width, rows[i].minx, rows[i].maxx});
			if (rows[n - 1].next <= start) break;
			start = rows[n - 1].next;
		}
		return out;
	}, vg, py::arg("string"), py::arg("break_row_width"));
}

} }
