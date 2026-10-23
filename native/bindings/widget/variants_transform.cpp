// SPDX-License-Identifier: GPL-3.0-or-later
#include "variants.hpp"
#include <pybind11/stl.h>
#include <array>

namespace rackpy { namespace widgets {
namespace {
using T = rack::widget::TransformWidget;
struct Matrix {
	std::shared_ptr<State> owner;
	T& get() const {
		auto* value = dynamic_cast<T*>(&checked(owner));
		if (!value) throw runtime::StaleHandle("TransformWidget native base has been destroyed");
		return *value;
	}
	std::array<float, 6> copy() const {
		auto& w = get();
		return {w.transform[0], w.transform[1], w.transform[2], w.transform[3], w.transform[4], w.transform[5]};
	}
};
int index(int i) {
	if (i < 0 || i >= 6) throw py::index_error("transform index must be in [0, 6)");
	return i;
}
}
void bindTransformVariants(py::module_ mod) {
	py::class_<Matrix>(mod, "TransformMatrix", py::is_final(), "Checked, nonowning six-float transform array; copy() returns a tuple.")
		.def("__len__", [](const Matrix& m) { m.get(); return 6; })
		.def("__iter__", [](const Matrix& m) { return py::iter(py::cast(m.copy())); })
		.def("copy", [](const Matrix& m) { return py::tuple(py::cast(m.copy())); })
		.def("__getitem__", [](const Matrix& m, int i) { return m.get().transform[index(i)]; })
		.def("__setitem__", [](const Matrix& m, int i, float value) { m.get().transform[index(i)] = value; });
	auto transform = variant<T>(mod, "TransformWidget");
	transform.def_property("transform", [](py::object self) { native<T>(self); return Matrix{state(self)}; },
		[](py::object self, std::array<float, 6> value) {
			auto& w = native<T>(self);
			std::copy(value.begin(), value.end(), w.transform);
		})
		.def("identity", [](py::object self) { native<T>(self).identity(); })
		.def("translate", [](py::object self, rack::math::Vec delta) { native<T>(self).translate(delta); }, py::arg("delta"))
		.def("scale", [](py::object self, rack::math::Vec scale) { native<T>(self).scale(scale); }, py::arg("scale"))
		.def("rotate", [](py::object self, float angle) { native<T>(self).rotate(angle); }, py::arg("angle"))
		.def("rotate", [](py::object self, float angle, rack::math::Vec origin) { native<T>(self).rotate(angle, origin); },
			py::arg("angle"), py::arg("origin"));
	using Z = rack::widget::ZoomWidget;
	auto zoom = variant<Z>(mod, "ZoomWidget");
	zoom.def_property("zoom", [](py::object self) { return native<Z>(self).zoom; },
		[](py::object self, float value) { native<Z>(self).zoom = value; })
		.def("get_zoom", [](py::object self) { return native<Z>(self).getZoom(); })
		.def("set_zoom", [](py::object self, float zoom) { checkTraversal(state(self)); native<Z>(self).setZoom(zoom); }, py::arg("zoom"));
}
} }
