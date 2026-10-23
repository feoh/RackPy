// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "trampoline.hpp"
#include "behavior.hpp"

namespace rackpy { namespace widgets {
template<class Base, class Parent = W>
auto variant(py::module_ mod, const char* name) {
	auto cls = py::classh<Base, Parent, Widget<Base>>(mod, name);
	cls.def(py::init([] {
		auto* value = new Widget<Base>();
		return std::unique_ptr<Base, UiDelete>(value, UiDelete{value->lifetime});
	}));
	bindBehavior(cls);
	return cls;
}
void bindEventVariants(py::module_ mod);
void bindTransformVariants(py::module_ mod);
void bindFramebufferVariants(py::module_ mod);
void bindSvgVariant(py::module_ raw, py::module_ mod);
inline void bindVariants(py::module_ raw, py::module_ mod) {
	bindEventVariants(mod);
	bindTransformVariants(mod);
	bindFramebufferVariants(mod);
	bindSvgVariant(raw, mod);
}
} }
