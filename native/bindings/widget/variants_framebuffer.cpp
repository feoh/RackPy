// SPDX-License-Identifier: GPL-3.0-or-later
#include "variants.hpp"
#include "../../widget/geometry.hpp"
#include <pybind11/stl.h>

namespace rackpy { namespace widgets {
namespace {
using F = rack::widget::FramebufferWidget;
using G = rack::widget::OpenGlWidget;

template<class Base, class... Options>
void framebufferCallback(py::classh<Base, Options...>& cls) {
	cls.def("draw_framebuffer", [](py::object self) {
		auto s = state(self);
		auto& w = native<Base>(self);
		checkTraversal(s);
		if (!w.getFramebuffer()) throw runtime::Ownership("render a framebuffer before calling its drawing base method");
		++s->graphicsGeneration;
		DrawGuard drawing(s);
		CallbackGuard guard;
		w.Base::drawFramebuffer();
	});
}
}
void bindFramebufferVariants(py::module_ mod) {
	auto cls = variant<F>(mod, "FramebufferWidget");
	framebufferCallback(cls);
	cls.def_property("dirty", [](py::object self) { return native<F>(self).dirty; }, [](py::object self, bool v) { native<F>(self).dirty = v; })
		.def_property("bypassed", [](py::object self) { return native<F>(self).bypassed; }, [](py::object self, bool v) { native<F>(self).bypassed = v; })
		.def_property("oversample", [](py::object self) { return native<F>(self).oversample; }, [](py::object self, float v) { native<F>(self).oversample = v; })
		.def_property("dirty_on_subpixel_change", [](py::object self) { return native<F>(self).dirtyOnSubpixelChange; },
			[](py::object self, bool v) { native<F>(self).dirtyOnSubpixelChange = v; })
		.def_property("viewport_margin", [](py::object self) {
			auto s = state(self);
			native<F>(self);
			widget::GeometryOwner owner(std::function<W&()>([s]() -> W& { return checked(s); }));
			return widget::VecView{owner, false, [s]() -> rack::math::Vec& {
				auto* f = dynamic_cast<F*>(&checked(s));
				if (!f) throw runtime::StaleHandle("FramebufferWidget native base has been destroyed");
				return f->viewportMargin;
			}};
		}, [](py::object self, rack::math::Vec v) { native<F>(self).viewportMargin = v; })
		.def("set_dirty", [](py::object self, bool dirty) { native<F>(self).setDirty(dirty); }, py::arg("dirty") = true)
		.def("get_image_handle", [](py::object self) { return native<F>(self).getImageHandle(); })
		.def("get_framebuffer_size", [](py::object self) { return native<F>(self).getFramebufferSize(); })
		.def("get_framebuffer", [](py::object self) -> py::object {
			auto s = state(self);
			auto* ptr = native<F>(self).getFramebuffer();
			if (!ptr) return py::none();
			auto generation = s->graphicsGeneration;
			auto scope = std::make_shared<Scope>();
			scope->context = s->token->context;
			scope->validate = [s, ptr, generation] {
				auto* f = dynamic_cast<F*>(&checked(s));
				if (!f || s->graphicsGeneration != generation || f->getFramebuffer() != ptr)
					throw runtime::StaleHandle("framebuffer was rendered, replaced, deleted or its window context changed");
			};
			return py::cast(Framebuffer{scope, ptr});
		})
		.def("delete_framebuffer", [](py::object self) {
			auto s = state(self);
			auto& w = native<F>(self);
			if (drawingActive()) throw runtime::Ownership("cannot delete graphics resources during drawing");
			++s->graphicsGeneration;
			w.deleteFramebuffer();
		})
		.def("render", [](py::object self, rack::math::Vec scale, rack::math::Vec offset, rack::math::Rect clip) {
			auto s = state(self);
			auto& w = native<F>(self);
			checkTraversal(s);
			if (drawingActive()) throw runtime::Ownership("explicit render cannot nest inside a drawing callback");
			++s->graphicsGeneration;
			DrawGuard drawing(s);
			CallbackGuard guard;
			validateFramebuffer(w, scale, offset, clip);
			w.render(scale, offset, clip);
			++s->graphicsGeneration;
		}, py::arg("scale") = rack::math::Vec(1.f, 1.f), py::arg("offset_f") = rack::math::Vec(),
			py::arg("clip_box") = rack::math::Rect::inf());
	auto gl = variant<G, F>(mod, "OpenGlWidget");
	framebufferCallback(gl);
}
} }
