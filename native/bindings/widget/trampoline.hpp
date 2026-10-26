#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
#include "events.hpp"
#include "drawing.hpp"
#include <optional>
namespace rackpy { namespace widgets {
// Give non-framebuffer aliases an internal slot too, so the concrete alias
// can use an explicit override without disabling -Wsuggest-override.
template<class Base, bool FramebufferBase = std::is_base_of<rack::widget::FramebufferWidget, Base>::value>
struct FramebufferSlot : Base { virtual void drawFramebuffer() {} };
template<class Base> struct FramebufferSlot<Base, true> : Base {};

// Always use an alias, including for the exact Python base class: Rack raw
// delete must invalidate retained Python instances via self-life-support.
template<class Base>
struct Widget : Managed, py::trampoline_self_life_support, Invalidate, FramebufferSlot<Base> {
	Widget() {
		lifetime = createState(this, static_cast<Base*>(this), typeid(Base));
		tracked = &lifetime;
		std::weak_ptr<State> weak = lifetime;
		auto* pointer = static_cast<Base*>(this);
		lifetime->disown = [weak](py::handle self) -> W* {
			auto s = weak.lock();
			if (!s) throw runtime::StaleHandle("Widget state has expired");
			checked(s);
			auto owned = self.cast<std::unique_ptr<Base, UiDelete>>();
			s->pythonOwned = false;
			return owned.release();
		};
		lifetime->reclaim = [weak, pointer] {
			auto s = weak.lock();
			if (!s) throw runtime::StaleHandle("Widget state has expired");
			checked(s);
			auto peer = py::cast(std::unique_ptr<Base, UiDelete>(pointer, UiDelete{s}));
			s->pythonOwned = true;
			return peer;
		};
	}
	~Widget() override { lifetime->clearing = true; destructionGuard.emplace(); }

	template<class Result, class F, class... Args>
	Result result(const char* name, F fallback, Args&&... args) {
		if (!runtime::isMainThread()) return Result{};
		CallbackGuard dispatch;
		std::optional<Result> value;
		runtime::callPython(name, [&] {
			checked(lifetime);
			if (auto f = py::get_override(static_cast<const Base*>(this), name)) {
				CallbackGuard guard;
				value = f(std::forward<Args>(args)...).template cast<Result>();
			}
		});
		if (value) return *value;
		try { CallbackGuard guard; return fallback(); }
		catch (...) { runtime::detail::noteRaised(name, "native Widget fallback failed"); return Result{}; }
	}
	rack::math::Rect getChildrenBoundingBox() override {
		return result<rack::math::Rect>("get_children_bounding_box", [this] { return Base::getChildrenBoundingBox(); });
	}
	rack::math::Rect getVisibleChildrenBoundingBox() override {
		return result<rack::math::Rect>("get_visible_children_bounding_box", [this] { return Base::getVisibleChildrenBoundingBox(); });
	}
	rack::math::Vec getRelativeOffset(rack::math::Vec value, W* ancestor) override {
		// Construct checked Python widget references only inside the GIL boundary.
		if (!runtime::isMainThread()) return {};
		CallbackGuard dispatch;
		std::optional<rack::math::Vec> valueOut;
		runtime::callPython("Widget.get_relative_offset", [&] {
			checked(lifetime);
			if (auto f = py::get_override(static_cast<const Base*>(this), "get_relative_offset")) {
				CallbackGuard guard;
				valueOut = f(value, wrapper(ancestor)).template cast<rack::math::Vec>();
			}
		});
		if (valueOut) return *valueOut;
		try { return Base::getRelativeOffset(value, ancestor); }
		catch (...) { runtime::detail::noteRaised("get_relative_offset", "native Widget fallback failed"); return {}; }
	}
	float getRelativeZoom(W* ancestor) override {
		if (!runtime::isMainThread()) return 1.f;
		CallbackGuard dispatch;
		std::optional<float> zoom;
		runtime::callPython("Widget.get_relative_zoom", [&] {
			checked(lifetime);
			if (auto f = py::get_override(static_cast<const Base*>(this), "get_relative_zoom")) {
				CallbackGuard guard;
				zoom = f(wrapper(ancestor)).template cast<float>();
			}
		});
		if (zoom) return *zoom;
		try { return Base::getRelativeZoom(ancestor); }
		catch (...) { runtime::detail::noteRaised("get_relative_zoom", "native Widget fallback failed"); return 1.f; }
	}
	rack::math::Rect getViewport(rack::math::Rect value) override {
		return result<rack::math::Rect>("get_viewport", [this, value] { return Base::getViewport(value); }, value);
	}
	void draw(NVGcontext* vg) override {
		if (!runtime::isMainThread()) return;
		CallbackGuard dispatch;
		bool answered = false;
		runtime::callPython("draw_vg", [&] {
			checked(lifetime);
			if (auto f = py::get_override(static_cast<const Base*>(this), "draw_vg")) {
				auto scope = std::make_shared<Scope>();
				scope->context = lifetime->token->context;
				ScopeEnd end{scope};
				DrawGuard drawing(lifetime);
				DrawScope nanovg{scope, vg};
				CallbackGuard guard;
				answered = true;
				f(Graphics{scope, vg});
			}
		});
		if (!answered) legacyDraw(*this, vg);
	}
	void draw(const W::DrawArgs& args) override { drawImpl(args, 0, false); }
	void drawLayer(const W::DrawArgs& args, int layer) override { drawImpl(args, layer, true); }
	void drawImpl(const W::DrawArgs& args, int layer, bool layered) noexcept {
		pyDraw(layered ? "draw_layer" : "draw", args,
			[&](py::function& f, py::object value) { if (layered) f(value, layer); else f(value); },
			[&] {
				if (layered) Base::drawLayer(args, layer);
				else { validateNativeDraw(*this, args); Base::draw(args); }
			});
	}
	/** A Rack drawing virtual (draw, drawLayer, LightWidget::drawLight...) that Python may
	override: the override gets a callback-scoped DrawArgs; otherwise Rack's own runs. */
	template<class Call, class Fallback>
	void pyDraw(const char* name, const W::DrawArgs& args, Call call, Fallback fallback) noexcept {
		if (!runtime::isMainThread()) return;
		CallbackGuard dispatch;
		++lifetime->graphicsGeneration;
		DrawGuard drawing(lifetime);
		bool answered = false;
		runtime::callPython(name, [&] {
			checked(lifetime);
			if (auto f = py::get_override(static_cast<const Base*>(this), name)) {
				auto scope = std::make_shared<Scope>();
				scope->context = lifetime->token->context;
				ScopeEnd end{scope};
				DrawScope nanovg{scope, args.vg};
				CallbackGuard guard;
				answered = true;
				call(f, eventObject(borrowedEvent(args, "DrawArgs", scope)));
			}
		});
		if (!answered) {
			try { CallbackGuard guard; fallback(); }
			catch (...) { runtime::detail::noteRaised(name, "native Widget draw failed"); }
		}
	}
	/** A void Rack virtual Python may override (ParamWidget::initParamQuantity...). */
	template<class Fallback, class... Args>
	void pyVoid(const char* name, Fallback fallback, Args&&... args) noexcept {
		if (!runtime::isMainThread()) return;
		CallbackGuard dispatch;
		bool answered = false;
		runtime::callPython(name, [&] {
			checked(lifetime);
			if (auto f = py::get_override(static_cast<const Base*>(this), name)) {
				CallbackGuard guard;
				answered = true;
				f(std::forward<Args>(args)...);
			}
		});
		if (!answered) {
			try { CallbackGuard guard; fallback(); }
			catch (...) { runtime::detail::noteRaised(name, "native Widget fallback failed"); }
		}
	}
	void step() override {
		if (!runtime::isMainThread())
			return;
		CallbackGuard dispatch(lifetime.get());
		bool overridden = false;
		runtime::callPython("Widget.step", [&] {
			checked(lifetime);
			CallbackGuard guard(lifetime.get());
			if (auto f = py::get_override(static_cast<const Base*>(this), "step")) {
				overridden = true;
				f();
			}
		});
		if (!overridden) {
			try { CallbackGuard guard; Base::step(); }
			catch (...) { runtime::detail::noteRaised("step", "native Widget step failed"); }
		}
	}
	// Only Framebuffer/OpenGl bases have this virtual. The unused member on
	// other aliases is not exported and does not enter Rack's vtable slots.
	void drawFramebuffer() override {
		if constexpr (std::is_base_of<rack::widget::FramebufferWidget, Base>::value) {
			if (!runtime::isMainThread()) return;
			CallbackGuard dispatch;
			++lifetime->graphicsGeneration;
			DrawGuard drawing(lifetime);
			bool answered = false;
			runtime::callPython("draw_framebuffer", [&] {
				checked(lifetime);
				if (auto f = py::get_override(static_cast<const Base*>(this), "draw_framebuffer")) {
					CallbackGuard guard;
					answered = true;
					f();
				}
			});
			if (!answered) {
				try { CallbackGuard guard; Base::drawFramebuffer(); }
				catch (...) { runtime::detail::noteRaised("draw_framebuffer", "native framebuffer callback failed"); }
			}
		}
	}
#define OVERRIDE(E, native, python) \
    void native(const W::E& e) override { \
        trampolineEvent(static_cast<const Base*>(this), lifetime, #python, #E, e, [this, &e] { Base::native(e); }); \
    }
    RACKPY_WIDGET_EVENTS(OVERRIDE)
#undef OVERRIDE
};
} }
