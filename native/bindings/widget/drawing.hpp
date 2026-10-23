// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "managed.hpp"
#include <cmath>
#include <limits>
#include <type_traits>

namespace rackpy { namespace widgets {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
inline void legacyDraw(W& widget, NVGcontext* vg) { widget.W::draw(vg); }
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
inline void validateFramebuffer(rack::widget::FramebufferWidget& w, rack::math::Vec scale,
	rack::math::Vec offset, rack::math::Rect clip) {
	auto* context = rack::contextGet();
	if (!context || !context->window || !context->window->vg || !context->window->fbVg)
		throw runtime::WrongContext("framebuffer operations require a live graphics window");
	if (!std::isfinite(w.oversample) || w.oversample <= 0.f)
		throw std::invalid_argument("framebuffer oversample must be finite and positive");
	auto local = w.children.empty() ? w.box.zeroPos() : w.getVisibleChildrenBoundingBox();
	auto expanded = clip.grow(w.viewportMargin);
	if (expanded.size.isFinite()) local = local.intersect(expanded);
	auto min = local.getTopLeft().mult(scale).plus(offset).floor();
	auto max = local.getBottomRight().mult(scale).plus(offset).ceil();
	auto size = max.minus(min).mult(std::fmax(1.f, std::floor(context->window->pixelRatio))).ceil();
	// Rack checks nonfinite persistent dimensions itself. Its finite float to
	// integer conversions (including the oversampled allocation) need bounds.
	if (!size.isFinite()) return;
	const double limit = std::numeric_limits<int>::max();
	for (float dimension : {size.x, size.y}) {
		double over = std::ceil(static_cast<double>(dimension) * w.oversample);
		if (std::abs(static_cast<double>(dimension)) > limit || !std::isfinite(over) || std::abs(over) > limit)
			throw std::invalid_argument("framebuffer dimensions exceed Rack's integer allocation range");
	}
	if (std::abs(static_cast<double>(size.x) * size.y) > limit)
		throw std::invalid_argument("framebuffer area exceeds Rack's integer accounting range");
}

template<class Base> void validateNativeDraw(Base& w, const W::DrawArgs& args) {
	if constexpr (std::is_base_of<rack::widget::SvgWidget, Base>::value) {
		if (w.svg && !w.svg->handle)
			throw std::invalid_argument("SvgWidget has an unloaded SVG; use None or a successfully loaded Svg");
	}
	if constexpr (std::is_base_of<rack::widget::FramebufferWidget, Base>::value) {
		if (w.bypassed || args.fb) return;
		float t[6];
		nvgCurrentTransform(args.vg, t);
		if (!rack::math::isNear(t[1], 0.f) || !rack::math::isNear(t[2], 0.f)) return;
		validateFramebuffer(w, rack::math::Vec(t[0], t[3]), rack::math::Vec(t[4], t[5]).minus(rack::math::Vec(t[4], t[5]).floor()), args.clipBox);
	}
}
} }
