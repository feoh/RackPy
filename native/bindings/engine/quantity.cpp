// SPDX-License-Identifier: GPL-3.0-or-later
// Checked built-in quantity adapters, not standalone/subclassable quantities.
#include "quantity.hpp"
#include "components.hpp"

#include <pybind11/stl.h>

#include <limits>
#include <optional>

#include "rackpy/runtime/json.hpp"
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
	return ownModuleField(get().module, lockOwner(state));
}

py::object quantityView(const ModuleRef& ref, int id) {
	auto& m = ref.get();
	auto ptr = m.getParamQuantity(id);
	if (!ptr)
		return py::none();
	if (module::transferredIn(ptr))
		return py::cast(ptr, py::return_value_policy::reference);  // the transferred object itself
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
	// Rack's engine indexes params with paramId unchecked; during construction
	// a raw edit may leave it out of range.
	if (q.module)
		module::Construction::index(q.paramId, q.module->params.size());
	auto context = runtime::binding().current();
	if (!context || rack::contextGet() != context->context)
		throw runtime::WrongContext("quantity value/display methods require Rack's live bound UI context");
	py::gil_scoped_release release;
	runtime::EngineLease lease(*context);
	if (!context->context->engine)
		throw runtime::WrongContext("quantity value/display methods require Rack's live bound UI context");
	return action(q);
}

void requireUnsmoothed(rack::engine::ParamQuantity& q) {
	// An unregistered module cannot be left in Rack's global smoothing slot:
	// Engine would retain a raw pointer after the owned Module is deleted.
	if (q.smoothEnabled && !module::smoothingAllowed(q.module))
		throw runtime::Ownership("smoothing targets require engine-owned modules; use set_immediate_value for this unregistered module");
}

void setValue(rack::engine::ParamQuantity& q, float value) {
	requireUnsmoothed(q);
	q.setValue(value);
}

void checkLabelIndex(rack::engine::ParamQuantity& q) {
	if (dynamic_cast<rack::engine::SwitchQuantity*>(&q)) {
		double index = std::floor(q.getValue() - q.getMinValue());
		if (!std::isfinite(index) || index < std::numeric_limits<int>::min() || index > std::numeric_limits<int>::max())
			throw std::invalid_argument("switch label index cannot be represented as an integer");
	}
}

/** Resolve the vector afresh on every operation, not a pointer into its
storage. Reallocation/whole-vector assignment keeps this alias valid; slot
replacement/owner deletion invalidates it through the quantity token. */
struct SwitchLabels {
	std::optional<SwitchQuantityView> view;
	// Owned labels keep the owner in their GC-visible Python instance dict,
	// not a hidden C++ strong PyObject edge (which would leak subclass cycles).
	// A weak native resolver also makes dict tampering safe, never dangling.
	py::weakref owned;
	std::vector<std::string>& get() const {
		if (view)
			return static_cast<rack::engine::SwitchQuantity&>(view->get()).labels;
		runtime::requireMainThread("using standalone switch labels");
		auto held = owned();
		if (held.is_none())
			throw runtime::StaleHandle("this standalone switch quantity was deleted");
		auto& q = held.cast<rack::engine::SwitchQuantity&>();
		guardQuantity(q);  // a slot-owned or retired quantity has stricter rules
		return q.labels;
	}
	py::ssize_t index(py::ssize_t i) const {
		auto n = static_cast<py::ssize_t>(get().size());
		if (i < 0)
			i += n;
		if (i < 0 || i >= n)
			throw py::index_error("switch label index out of range");
		return i;
	}
};

template <typename T>
void field(py::class_<QuantityView>& cls, const char* name, T rack::engine::ParamQuantity::*member) {
	cls.def_property(name, [member](const QuantityView& v) { return v.get().*member; },
		[member](const QuantityView& v, T value) { v.get().*member = std::move(value); });
}

}  // namespace

py::object ownedSwitchLabels(py::object owner) {
	runtime::requireMainThread("using standalone switch labels");
	auto& labels = owner.cast<rack::engine::SwitchQuantity&>().labels;
	return runtime::identity(&labels, 0, [&] {
		auto result = py::cast(SwitchLabels{std::nullopt, py::weakref(owner)});
		result.attr("_rackpy_owner") = owner;
		return result;
	});
}

void bindQuantityViews(py::module_ engine) {
	using Q = rack::engine::ParamQuantity;
	py::class_<SwitchLabels>(engine, "SwitchLabels", py::dynamic_attr(),
		"A checked mutable alias of a switch quantity's label vector.")
		.def("__len__", [](const SwitchLabels& v) { return v.get().size(); })
		.def("__getitem__", [](const SwitchLabels& v, py::ssize_t i) { return v.get()[v.index(i)]; })
		.def("__setitem__", [](const SwitchLabels& v, py::ssize_t i, std::string s) { v.get()[v.index(i)] = std::move(s); })
		.def("__delitem__", [](const SwitchLabels& v, py::ssize_t i) {
			auto index = v.index(i);
			v.get().erase(v.get().begin() + index);
		})
		.def("insert", [](const SwitchLabels& v, py::ssize_t i, std::string s) {
			auto& labels = v.get();
			auto n = static_cast<py::ssize_t>(labels.size());
			if (i < 0)
				i += n;
			i = std::max(py::ssize_t(0), std::min(i, n));
			labels.insert(labels.begin() + i, std::move(s));
		}, py::arg("index"), py::arg("label"))
		.def("append", [](const SwitchLabels& v, std::string s) { v.get().push_back(std::move(s)); }, py::arg("label"))
		.def("extend", [](const SwitchLabels& v, std::vector<std::string> items) {
			// Materialize/convert all items before touching the destination.
			auto& labels = v.get();
			labels.insert(labels.end(), items.begin(), items.end());
		}, py::arg("labels"))
		.def("clear", [](const SwitchLabels& v) { v.get().clear(); })
		.def("tolist", [](const SwitchLabels& v) { return v.get(); })
		.def("__iter__", [](const SwitchLabels& v) { return py::iter(py::cast(v.get())); },
			"Iterate a copied snapshot, not invalidatable native vector iterators.");

	auto cls = py::class_<QuantityView>(engine, "ParamQuantityView",
		"Checked built-in module quantity; not a standalone Quantity or Python virtual trampoline.");
	cls.def_property_readonly("alive", [](const QuantityView& v) { return v.token->alive(); })
		.def_property("module", &QuantityView::owner, [](const QuantityView& v, py::handle value) {
			setOwnModuleField(v.get().module, value, lockOwner(v.state));
		}, "The owning module or None; assignable only during construction, to those two values.")
		.def_property("param_id", [](const QuantityView& v) { return v.get().paramId; }, [](const QuantityView& v, int id) {
			v.get();
			lockOwner(v.state)->requireConstruction();
			v.get().paramId = id;
		})
		.def("get_param", [](const QuantityView& v) -> py::object {
			auto& q = v.get();
			auto owner = v.state.lock();
			if (!owner)
				throw runtime::StaleHandle("this quantity belonged to a deleted module");
			auto* param = q.getParam();  // null for an ID a raw edit left out of range
			return param ? borrowParam(param, owner->get().paramsToken) : py::none();
		})
		.def("get_min_value", [](const QuantityView& v) { return v.get().getMinValue(); })
		.def("get_max_value", [](const QuantityView& v) { return v.get().getMaxValue(); })
		.def("get_default_value", [](const QuantityView& v) { return v.get().getDefaultValue(); })
		.def("get_display_precision", [](const QuantityView& v) { return v.get().getDisplayPrecision(); })
		.def("get_label", [](const QuantityView& v) { return v.get().getLabel(); })
		.def("get_unit", [](const QuantityView& v) { return v.get().getUnit(); })
		.def("get_description", [](const QuantityView& v) { return v.get().getDescription(); })
		.def("get_range", [](const QuantityView& v) { return v.get().getRange(); })
		.def("is_bounded", [](const QuantityView& v) { return v.get().isBounded(); })
		.def("to_scaled", [](const QuantityView& v, float x) { return v.get().toScaled(x); }, py::arg("value"))
		.def("from_scaled", [](const QuantityView& v, float x) { return v.get().fromScaled(x); }, py::arg("scaled_value"))
		.def("get_string", [](const QuantityView& v) {
			return withEngine(v, [](Q& q) { checkLabelIndex(q); return q.getString(); });
		})
		.def("is_min", [](const QuantityView& v) { return withEngine(v, [](Q& q) { return q.isMin(); }); })
		.def("is_max", [](const QuantityView& v) { return withEngine(v, [](Q& q) { return q.isMax(); }); })
		.def("get_scaled_value", [](const QuantityView& v) { return withEngine(v, [](Q& q) { return q.getScaledValue(); }); })
		.def("set_min", [](const QuantityView& v) { withEngine(v, [](Q& q) { requireUnsmoothed(q); q.setMin(); }); })
		.def("set_max", [](const QuantityView& v) { withEngine(v, [](Q& q) { requireUnsmoothed(q); q.setMax(); }); })
		.def("toggle", [](const QuantityView& v) { withEngine(v, [](Q& q) { requireUnsmoothed(q); q.toggle(); }); })
		.def("move_value", [](const QuantityView& v, float x) {
			withEngine(v, [x](Q& q) { requireUnsmoothed(q); q.moveValue(x); });
		}, py::arg("delta_value"))
		.def("set_scaled_value", [](const QuantityView& v, float x) {
			withEngine(v, [x](Q& q) { requireUnsmoothed(q); q.setScaledValue(x); });
		}, py::arg("scaled_value"))
		.def("move_scaled_value", [](const QuantityView& v, float x) {
			withEngine(v, [x](Q& q) { requireUnsmoothed(q); q.moveScaledValue(x); });
		}, py::arg("delta_scaled_value"))
		.def("get_value", [](const QuantityView& v) { return withEngine(v, [](Q& q) { return q.getValue(); }); })
		.def("set_value", [](const QuantityView& v, float x) { withEngine(v, [x](Q& q) { setValue(q, x); }); }, py::arg("value"))
		.def("get_immediate_value", [](const QuantityView& v) { return withEngine(v, [](Q& q) { return q.getImmediateValue(); }); })
		.def("set_immediate_value", [](const QuantityView& v, float x) { withEngine(v, [x](Q& q) { q.setImmediateValue(x); }); }, py::arg("value"))
		.def("get_display_value", [](const QuantityView& v) { return withEngine(v, [](Q& q) { return q.getDisplayValue(); }); })
		.def("set_display_value", [](const QuantityView& v, float x) { withEngine(v, [x](Q& q) { q.setDisplayValue(x); }); }, py::arg("display_value"))
		.def("get_display_value_string", [](const QuantityView& v) {
			return withEngine(v, [](Q& q) {
				checkLabelIndex(q);
				return q.getDisplayValueString();
			});
		})
		.def("set_display_value_string", [](const QuantityView& v, std::string s) { withEngine(v, [&s](Q& q) { q.setDisplayValueString(s); }); }, py::arg("s"))
		.def("reset", [](const QuantityView& v) { withEngine(v, [](Q& q) { q.reset(); }); })
		.def("to_json", [](const QuantityView& v) {
			json_t* json = withEngine(v, [](Q& q) { return q.toJson(); });
			std::unique_ptr<json_t, decltype(&json_decref)> owner(json, json_decref);
			return runtime::fromJson(json);
		})
		.def("from_json", [](const QuantityView& v, py::handle data) {
			std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(data), json_decref);
			withEngine(v, [&](Q& q) { q.fromJson(json.get()); });
		}, py::arg("data"))
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
			auto& labels = static_cast<rack::engine::SwitchQuantity&>(v.get()).labels;
			return runtime::identity(&labels, v.token->generation, [&] { return py::cast(SwitchLabels{v, {}}); });
		}, [](const SwitchQuantityView& v, std::vector<std::string> labels) {
			static_cast<rack::engine::SwitchQuantity&>(v.get()).labels = std::move(labels);
		}, "Checked mutable vector alias; assign a complete sequence to replace its contents.");
}

}  // namespace bindings
}  // namespace rackpy
