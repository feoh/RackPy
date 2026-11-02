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

/** Rack's Scrollbar code asserts that its parent is a ScrollWidget (Scrollbar.cpp draw, onButton, onDragMove). */
template<class Base> void requireScrollParent(Base& w, const char* what) {
	if (!::rackpy::rttiCast<rack::ui::ScrollWidget*>(w.parent))
		throw runtime::Ownership(std::string("a Scrollbar's ") + what + " needs a ScrollWidget parent: Rack's code asserts it");
}

/** What Rack's own code would assert or dereference before a widget's step runs (RP-42): a Menu nudges itself inside its
 parent and asserts it (Menu.cpp:52), a MenuOverlay adopts its parent's size without a check (MenuOverlay.cpp:27), a
 Tooltip nudges itself inside its parent and asserts it (Tooltip.cpp:19). */
template<class Base> void checkNativeStep(Base& w) {
	if constexpr (std::is_base_of<rack::ui::Menu, Base>::value || std::is_base_of<rack::ui::MenuOverlay, Base>::value ||
		std::is_base_of<rack::ui::Tooltip, Base>::value) {
		if (!w.parent)
			throw runtime::Ownership("step needs this widget to have a parent (a MenuOverlay or the scene for a menu, the "
				"scene's root for a tooltip): Rack's code uses it");
	}
}

/** The same for events (RP-42): a Scrollbar's left press and drag move assert a ScrollWidget parent; a MenuItem's
 enter opens its child menu through its Menu's setChildMenu, which asserts that the Menu has a parent (Menu.cpp:24). */
template<class Base, class E> void checkNativeEvent(Base& w) {
	if constexpr (std::is_base_of<rack::ui::Scrollbar, Base>::value &&
		(std::is_same<E, W::DragMoveEvent>::value || std::is_same<E, W::ButtonEvent>::value)) {
		requireScrollParent(w, std::is_same<E, W::ButtonEvent>::value ? "on_button" : "on_drag_move");
	}
	if constexpr (std::is_base_of<rack::ui::MenuItem, Base>::value && std::is_same<E, W::EnterEvent>::value) {
		auto* menu = ::rackpy::rttiCast<rack::ui::Menu*>(w.parent);
		if (menu && !menu->parent)
			throw runtime::Ownership("on_enter needs its Menu to have a parent: Rack's code opens the child menu there");
	}
}

template<class Base> void validateNativeDraw(Base& w, const W::DrawArgs& args) {
	if constexpr (std::is_base_of<rack::ui::Scrollbar, Base>::value)
		requireScrollParent(w, "draw");
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
