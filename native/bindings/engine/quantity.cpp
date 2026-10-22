// SPDX-License-Identifier: GPL-3.0-or-later
// Checked built-in quantity adapters, not standalone/subclassable quantities.
#include "quantity.hpp"
#include "components.hpp"

#include <pybind11/stl.h>

#include <limits>

#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace bindings {

rack::engine::ParamQuantity& QuantityView::get() const {
	runtime::requireMainThread("using a module quantity");
	if (!token->alive())
		throw runtime::StaleHandle("this quantity was replaced or its module was deleted");
	return *ptr;
}

py::object QuantityView::owner() const {
	get();
	auto held = state.lock();
	if (!held)
		throw runtime::StaleHandle("this quantity belonged to a deleted module");
	auto& m = held->get();
	return runtime::identity(&m, m.token->generation, [&] { return py::cast(ModuleRef{held}); });
}

py::object quantityView(const ModuleRef& ref, int id) {
	auto& m = ref.get();
	auto ptr = m.getParamQuantity(id);
	if (!ptr)
		return py::none();
	auto token = m.paramQuantityTokens[id];
	return runtime::identity(ptr, token->generation, [&]() -> py::object {
		QuantityView view{ptr, token, ref.state};
		if (dynamic_cast<rack::engine::SwitchQuantity*>(ptr))
			return py::cast(SwitchQuantityView{view});
		return py::cast(view);
	});
}

namespace {

/** These methods call through APP->engine. Pin the unregistered owner across
GIL release, validate the active context, and hold its teardown lease. No
Python override exists. Metadata-only methods do not need a Rack context. */
template <typename F>
auto withEngine(const QuantityView& view, F action) -> decltype(action(view.get())) {
	auto& q = view.get();
	auto owner = view.state.lock();
	if (!owner)
		throw runtime::StaleHandle("this quantity belonged to a deleted module");
	auto context = runtime::binding().current();
	if (!context || rack::contextGet() != context->context)
		throw runtime::WrongContext("quantity value/display methods require Rack's live bound UI context");
	py::gil_scoped_release release;
	runtime::EngineLease lease(*context);
	if (!context->context->engine)
		throw runtime::WrongContext("quantity value/display methods require Rack's live bound UI context");
	return action(q);
}

void setValue(rack::engine::ParamQuantity& q, float value) {
	// An unregistered module cannot be left in Rack's global smoothing slot:
	// Engine would retain a raw pointer after the owned Module is deleted.
	if (q.smoothEnabled)
		throw runtime::Ownership("smoothing targets require engine-owned modules; use set_immediate_value for this unregistered module");
	q.setValue(value);
}

template <typename T>
void field(py::class_<QuantityView>& cls, const char* name, T rack::engine::ParamQuantity::*member) {
	cls.def_property(name, [member](const QuantityView& v) { return v.get().*member; },
		[member](const QuantityView& v, T value) { v.get().*member = std::move(value); });
}

}  // namespace

void bindQuantityViews(py::module_ engine) {
	using Q = rack::engine::ParamQuantity;
	auto cls = py::class_<QuantityView>(engine, "ParamQuantityView",
		"Checked built-in module quantity; not a standalone Quantity or Python virtual trampoline.");
	cls.def_property_readonly("alive", [](const QuantityView& v) { return v.token->alive(); })
		.def_property_readonly("module", &QuantityView::owner)
		.def_property_readonly("param_id", [](const QuantityView& v) { return v.get().paramId; })
		.def("get_param", [](const QuantityView& v) {
			auto& q = v.get();
			auto owner = v.state.lock();
			if (!owner)
				throw runtime::StaleHandle("this quantity belonged to a deleted module");
			return borrowParam(q.getParam(), owner->get().token);
		})
		.def("get_min_value", [](const QuantityView& v) { return v.get().getMinValue(); })
		.def("get_max_value", [](const QuantityView& v) { return v.get().getMaxValue(); })
		.def("get_default_value", [](const QuantityView& v) { return v.get().getDefaultValue(); })
		.def("get_display_precision", [](const QuantityView& v) { return v.get().getDisplayPrecision(); })
		.def("get_label", [](const QuantityView& v) { return v.get().getLabel(); })
		.def("get_unit", [](const QuantityView& v) { return v.get().getUnit(); })
		.def("get_description", [](const QuantityView& v) { return v.get().getDescription(); })
		.def("get_value", [](const QuantityView& v) { return withEngine(v, [](Q& q) { return q.getValue(); }); })
		.def("set_value", [](const QuantityView& v, float x) { withEngine(v, [x](Q& q) { setValue(q, x); }); }, py::arg("value"))
		.def("get_immediate_value", [](const QuantityView& v) { return withEngine(v, [](Q& q) { return q.getImmediateValue(); }); })
		.def("set_immediate_value", [](const QuantityView& v, float x) { withEngine(v, [x](Q& q) { q.setImmediateValue(x); }); }, py::arg("value"))
		.def("get_display_value", [](const QuantityView& v) { return withEngine(v, [](Q& q) { return q.getDisplayValue(); }); })
		.def("set_display_value", [](const QuantityView& v, float x) { withEngine(v, [x](Q& q) { q.setDisplayValue(x); }); }, py::arg("display_value"))
		.def("get_display_value_string", [](const QuantityView& v) {
			return withEngine(v, [](Q& q) {
				if (dynamic_cast<rack::engine::SwitchQuantity*>(&q)) {
					double index = std::floor(q.getValue() - q.getMinValue());
					if (!std::isfinite(index) || index < std::numeric_limits<int>::min() || index > std::numeric_limits<int>::max())
						throw std::invalid_argument("switch label index cannot be represented as an integer");
				}
				return q.getDisplayValueString();
			});
		})
		.def("set_display_value_string", [](const QuantityView& v, std::string s) { withEngine(v, [&s](Q& q) { q.setDisplayValueString(s); }); }, py::arg("s"))
		.def("reset", [](const QuantityView& v) { withEngine(v, [](Q& q) { q.reset(); }); })
		.def("randomize", [](const QuantityView& v) { withEngine(v, [](Q& q) { q.randomize(); }); })
		.def("set_smooth_value", [](const QuantityView& v, float x) {
			v.get();
			if (PyErr_WarnEx(PyExc_DeprecationWarning, "set_smooth_value is deprecated in Rack; use set_value", 1) < 0)
				throw py::error_already_set();
			withEngine(v, [x](Q& q) { setValue(q, x); });
		}, py::arg("value"))
		.def("get_smooth_value", [](const QuantityView& v) {
			v.get();
			if (PyErr_WarnEx(PyExc_DeprecationWarning, "get_smooth_value is deprecated in Rack; use get_value", 1) < 0)
				throw py::error_already_set();
			return withEngine(v, [](Q& q) { return q.getValue(); });
		});
	field(cls, "min_value", &Q::minValue);
	field(cls, "max_value", &Q::maxValue);
	field(cls, "default_value", &Q::defaultValue);
	field(cls, "name", &Q::name);
	field(cls, "unit", &Q::unit);
	field(cls, "display_base", &Q::displayBase);
	field(cls, "display_multiplier", &Q::displayMultiplier);
	field(cls, "display_offset", &Q::displayOffset);
	field(cls, "display_precision", &Q::displayPrecision);
	field(cls, "description", &Q::description);
	field(cls, "reset_enabled", &Q::resetEnabled);
	field(cls, "randomize_enabled", &Q::randomizeEnabled);
	field(cls, "smooth_enabled", &Q::smoothEnabled);
	field(cls, "snap_enabled", &Q::snapEnabled);

	py::class_<SwitchQuantityView, QuantityView>(engine, "SwitchQuantityView")
		.def_property("labels", [](const SwitchQuantityView& v) {
			return static_cast<rack::engine::SwitchQuantity&>(v.get()).labels;
		}, [](const SwitchQuantityView& v, std::vector<std::string> labels) {
			static_cast<rack::engine::SwitchQuantity&>(v.get()).labels = std::move(labels);
		}, "Copied list; assign a complete sequence to replace labels. Not a mutable vector alias.");
}

}  // namespace bindings
}  // namespace rackpy
