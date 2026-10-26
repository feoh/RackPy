// SPDX-License-Identifier: GPL-3.0-or-later
// rack::app::ModuleWidget -> rackpy.raw.app.ModuleWidget, and Python module widget
// factories (RP-28B). Main thread only.
//
// A ModuleWidget owns its module: Rack's setModule deletes a module it
// replaces, after Engine::removeModule, which assumes the module is in the
// engine and has no cables. So a Python ModuleWidget takes only the module
// Rack hands its widget factory (Rack's createModel<TModule, TModuleWidget>
// passes it to the widget's constructor), once. A factory that fails gives
// the module back, untouched, and Rack gets RackPy's plain panel instead.
#include <rack.hpp>

#include <pybind11/stl.h>

#include <unordered_map>

#include "../../plugin/model.hpp"
#include "../engine/module.hpp"
#include "../plugin.hpp"
#include "app.hpp"
#include "app_internal.hpp"
#include "rackpy/runtime/json.hpp"

namespace pybind11 {
template <>
struct polymorphic_type_hook<rack::app::ModuleWidget> {
	static const void* get(const rack::app::ModuleWidget* ptr, const std::type_info*& type) {
		type = &typeid(rack::app::ModuleWidget);
		return ptr;
	}
};
}  // namespace pybind11

namespace rackpy {
namespace widgets {
namespace appdetail {

namespace {

using MW = rack::app::ModuleWidget;
using rack::engine::Module;

/** While a widget factory runs: the module Rack gave it, and the Python widgets that took it. */
struct FactoryCall {
	Module* module = nullptr;
	std::vector<std::weak_ptr<State>> claimed;
	FactoryCall* previous = nullptr;
};
FactoryCall* currentCall = nullptr;

struct CallScope {
	FactoryCall call;
	explicit CallScope(Module* module) {
		call.module = module;
		call.previous = currentCall;
		currentCall = &call;
	}
	~CallScope() { currentCall = call.previous; }
	CallScope(const CallScope&) = delete;
	CallScope& operator=(const CallScope&) = delete;
};

/** Python module widgets that own a module, so no second widget takes it. */
std::unordered_map<const Module*, std::weak_ptr<State>> owners;

bool ownedElsewhere(const Module* module, const State* self) {
	auto it = owners.find(module);
	if (it == owners.end())
		return false;
	auto s = it->second.lock();
	if (!s || s.get() == self || !s->native)
		return false;
	auto* mw = dynamic_cast<MW*>(s->native);
	return mw && mw->module == module;
}

/** Gives a failed or rejected factory call's module back: no widget but `keep` may delete it. */
void releaseClaims(FactoryCall& call, const State* keep) noexcept {
	for (auto& weak : call.claimed) {
		auto s = weak.lock();
		if (!s || s.get() == keep || !s->native)
			continue;
		if (auto* mw = dynamic_cast<MW*>(s->native); mw && mw->module == call.module) {
			mw->module = nullptr;  // so its destructor does not delete Rack's module
			owners.erase(call.module);
		}
	}
}

/** Takes `value` (a RackPy module) as this widget's module, as only a widget factory may. */
void claim(const std::shared_ptr<State>& s, MW& mw, py::handle value) {
	runtime::requireMainThread("setting a module widget's module");
	if (value.is_none()) {
		if (mw.module)
			throw runtime::Ownership("Rack deletes a module widget's module with it; set_module cannot drop or replace it");
		return;
	}
	if (mw.module)
		throw runtime::Ownership("Rack deletes a module widget's module with it; set_module cannot drop or replace it");
	if (!py::isinstance<bindings::ModuleRef>(value))
		throw py::type_error("set_module takes a rackpy.raw.engine.Module or None");
	Module* module = &value.cast<const bindings::ModuleRef&>().get();
	if (!currentCall || currentCall->module != module)
		throw runtime::Ownership("a ModuleWidget takes only the module Rack passes its widget factory");
	if (ownedElsewhere(module, s.get()))
		throw runtime::Ownership("another module widget already took this module");
	currentCall->claimed.push_back(s);
	owners[module] = s;
	mw.setModule(module);  // it had none, so Rack deletes nothing
}

template <class Base>
struct ModuleWidgetT : Widget<Base> {
	void appendContextMenu(rack::ui::Menu* menu) override { appendMenu<Base>(this, menu, nullptr); }
};

MW& inRack(py::object self, const char* action) {
	auto& mw = native<MW>(self);
	if (!mw.module)
		throw runtime::Ownership(std::string(action) + " needs the module widget's module");
	auto* context = rack::contextGet();
	if (!context || !context->scene || !context->scene->rack || context->scene->rack->getModule(mw.module->id) != &mw)
		throw runtime::Ownership(std::string(action) + " works on a module widget in Rack's rack");
	return mw;
}

MW& withModule(py::object self, const char* action) {
	auto& mw = native<MW>(self);
	if (!mw.module)
		throw runtime::Ownership(std::string(action) + " needs the module widget's module");
	return mw;
}

MW& withModel(py::object self, const char* action) {
	auto& mw = native<MW>(self);
	if (!mw.model)
		throw runtime::Ownership(std::string(action) + " needs the module widget's model");
	return mw;
}

/** Runs a Rack action that takes the engine lock, without the GIL (ADR 003, decision 2). */
template <class F>
auto unlocked(F f) {
	py::gil_scoped_release release;
	return f();
}

py::list wrappers(const std::vector<rack::app::PortWidget*>& ports) {
	py::list out;
	for (auto* p : ports)
		out.append(wrapper(p));
	return out;
}

/** The Python factory's widget for `module` (or None), checked as Rack's createModel asserts. */
py::object build(py::handle fn, rack::plugin::Model& model, Module* module) {
	py::object argument = py::none();
	if (module) {
		argument = bindings::engineModuleObject(module);
		if (argument.is_none())
			throw runtime::Ownership("this module is not a RackPy module Rack's engine owns");
	}
	CallScope scope(module);
	try {
		FactoryScope building;  // building a new, detached tree is allowed even inside Rack's event dispatch
		py::object widget = fn(argument);
		if (!py::isinstance(widget, py::module_::import("_rackpy_host").attr("raw").attr("app").attr("ModuleWidget")))
			throw py::type_error("a widget factory must return a rackpy.raw.app.ModuleWidget");
		auto s = state(widget);
		auto& mw = native<MW>(widget);
		if (mw.parent)
			throw runtime::Ownership("a widget factory must return a new module widget, with no parent");
		if (mw.module != module)
			throw runtime::Ownership("a widget factory must give its widget the module it was passed: call "
				"self.set_module(module) first, as Rack's module widgets do");
		if (mw.model)
			throw runtime::Ownership("a widget factory must not set the model: Rack sets it");
		mw.setModel(&model);
		for (auto* port : mw.getPorts())
			if (auto ps = managedState(port))
				markFactoryPort(ps);
		releaseClaims(scope.call, s.get());
		return widget;
	}
	catch (...) {
		releaseClaims(scope.call, nullptr);
		throw;
	}
}

/** Python's module widget factory for one model. Rack calls it on its main thread. */
struct PyWidgetFactory : plugin::ModuleWidgetFactory {
	runtime::PyRef fn;
	MW* create(rack::plugin::Model& model, Module* module) noexcept override {
		MW* result = nullptr;
		runtime::callPython("Model.create_module_widget", [&] {
			if (!fn)
				throw std::runtime_error("the plugin's Python factories have been released");
			py::object widget = build(fn.get(), model, module);
			try {
				result = dynamic_cast<MW*>(releaseDetached(widget));  // Rack owns it from here
			}
			catch (...) {
				native<MW>(widget).module = nullptr;  // give Rack its module back untouched
				throw;
			}
		});
		return result;
	}
	void release() noexcept override { fn.reset(); }
};

}  // namespace

void bindModuleWidget(py::module_ app) {
	auto cls = variant<MW, rack::widget::OpaqueWidget, ModuleWidgetT<MW>>(app, "ModuleWidget");
	cls.def(py::init([](py::object module) {
			if (PyErr_WarnEx(PyExc_DeprecationWarning, "ModuleWidget(module) mirrors a deprecated constructor; use "
				"ModuleWidget() and set_module(module)", 1) < 0)
				throw py::error_already_set();
			auto* value = new ModuleWidgetT<MW>();
			std::unique_ptr<MW, UiDelete> owned(value, UiDelete{value->lifetime});
			claim(value->lifetime, *value, module);
			return owned;
		}), py::arg("module"))
		.def_property_readonly("model", [](py::object self) { return bindings::modelObject(native<MW>(self).model); },
			"Rack's Model of this widget (not owned), set by Rack when it creates the widget.")
		.def_property_readonly("module", [](py::object self) { return bindings::engineModuleObject(native<MW>(self).module); },
			"The module this widget owns, or None (a module browser preview).")
		.def("get_model", [](py::object self) { return bindings::modelObject(native<MW>(self).getModel()); })
		.def("set_model", [](py::object self, py::object model) {
			auto& mw = native<MW>(self);
			if (mw.model)
				throw runtime::Ownership("this module widget already has its model");
			mw.setModel(bindings::modelArgument(model));
		}, py::arg("model"), "Rack's setModel, once; Rack sets it itself for the widgets its factories make.")
		.def("get_module", [](py::object self) { return bindings::engineModuleObject(native<MW>(self).getModule()); })
		.def("set_module", [](py::object self, py::object module) { claim(state(self), native<MW>(self), module); },
			py::arg("module"),
			"Takes the module Rack passed this widget's factory (or None for a browser preview). The widget then owns "
			"it: Rack deletes it with the widget. It cannot be replaced or dropped.")
		.def("get_panel", [](py::object self) { return wrapper(native<MW>(self).getPanel()); })
		.def("set_panel", [](py::object self, py::object panel) {
			auto& mw = native<MW>(self);
			if (panel.is_none()) {
				checkMutationOf(mw);
				CallbackGuard guard;
				mw.setPanel(static_cast<rack::widget::Widget*>(nullptr));
				return;
			}
			if (py::isinstance<rack::window::Svg>(panel)) {
				checkMutationOf(mw);
				auto svg = drawableSvg(panel.cast<std::shared_ptr<rack::window::Svg>>());
				CallbackGuard guard;
				mw.setPanel(std::move(svg));
				return;
			}
			transferWith(mw, panel, [&mw](W* p) { mw.setPanel(p); });
		}, py::arg("panel"),
			"Rack's setPanel: a Python widget (the module widget then owns it) or a loaded Svg (Rack makes an SvgPanel), "
			"replacing and deleting the previous panel. The widget's width snaps to the rack grid.")
		.def("add_param", [](py::object self, py::object param) {
			auto& mw = native<MW>(self);
			if (!dynamic_cast<rack::app::ParamWidget*>(argument(param)))
				throw py::type_error("add_param takes a rackpy.raw.app.ParamWidget");
			transferWith(mw, param, [&mw](W* p) { mw.addParam(static_cast<rack::app::ParamWidget*>(p)); });
		}, py::arg("param"))
		.def("add_input", [](py::object self, py::object port) {
			auto& mw = native<MW>(self);
			auto* p = dynamic_cast<rack::app::PortWidget*>(argument(port));
			if (!p)
				throw py::type_error("add_input takes a rackpy.raw.app.PortWidget");
			if (p->type != rack::engine::Port::INPUT)
				throw std::invalid_argument("add_input takes an input port (Rack asserts it)");
			if (mw.getInput(p->portId))
				throw std::invalid_argument("this module widget already has input " + std::to_string(p->portId));
			transferWith(mw, port, [&mw](W* w) { mw.addInput(static_cast<rack::app::PortWidget*>(w)); });
		}, py::arg("input"))
		.def("add_output", [](py::object self, py::object port) {
			auto& mw = native<MW>(self);
			auto* p = dynamic_cast<rack::app::PortWidget*>(argument(port));
			if (!p)
				throw py::type_error("add_output takes a rackpy.raw.app.PortWidget");
			if (p->type != rack::engine::Port::OUTPUT)
				throw std::invalid_argument("add_output takes an output port (Rack asserts it)");
			if (mw.getOutput(p->portId))
				throw std::invalid_argument("this module widget already has output " + std::to_string(p->portId));
			transferWith(mw, port, [&mw](W* w) { mw.addOutput(static_cast<rack::app::PortWidget*>(w)); });
		}, py::arg("output"))
		.def("get_param", [](py::object self, int id) { return wrapper(native<MW>(self).getParam(id)); }, py::arg("param_id"),
			"The first ParamWidget with this ID among the widget's descendants, or None.")
		.def("get_input", [](py::object self, int id) { return wrapper(native<MW>(self).getInput(id)); }, py::arg("port_id"))
		.def("get_output", [](py::object self, int id) { return wrapper(native<MW>(self).getOutput(id)); }, py::arg("port_id"))
		.def("get_params", [](py::object self) {
			py::list out;
			for (auto* p : native<MW>(self).getParams())
				out.append(wrapper(p));
			return out;
		})
		.def("get_ports", [](py::object self) { return wrappers(native<MW>(self).getPorts()); })
		.def("get_inputs", [](py::object self) { return wrappers(native<MW>(self).getInputs()); })
		.def("get_outputs", [](py::object self) { return wrappers(native<MW>(self).getOutputs()); })
		.def("append_context_menu", [](py::object self, py::object menu) { native<MW>(self).MW::appendContextMenu(menuArgument(menu)); },
			py::arg("menu"), "Rack's default adds nothing. Override to add items to the module's context menu.")
		.def("to_json", [](py::object self) {
			auto& mw = withModule(self, "to_json");
			json_t* data = unlocked([&] { return mw.toJson(); });
			std::unique_ptr<json_t, decltype(&json_decref)> held(data, json_decref);
			return runtime::fromJson(data);
		}, "Rack's: the module's patch JSON (Engine::moduleToJson).")
		.def("from_json", [](py::object self, py::handle data) {
			auto& mw = withModule(self, "from_json");
			std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(data), json_decref);
			unlocked([&] { mw.fromJson(json.get()); });
		}, py::arg("data"), "Rack's: loads the module from patch JSON (Engine::moduleFromJson).")
		.def("paste_json_action", [](py::object self, py::handle data) {
			auto& mw = inRack(self, "paste_json_action");
			std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(data), json_decref);
			return unlocked([&] { return mw.pasteJsonAction(json.get()); });
		}, py::arg("data"), "Rack's paste of module JSON, with an undo step. False if Rack refused it.")
		.def("copy_clipboard", [](py::object self) {
			auto& mw = withModule(self, "copy_clipboard");
			unlocked([&] { mw.copyClipboard(); });
		})
		.def("paste_clipboard_action", [](py::object self) {
			auto& mw = inRack(self, "paste_clipboard_action");
			return unlocked([&] { return mw.pasteClipboardAction(); });
		})
		.def("load", [](py::object self, const std::string& filename) {
			auto& mw = withModule(self, "load");
			unlocked([&] { mw.load(filename); });
		}, py::arg("filename"), "Rack's: loads a .vcvm preset; raises rackpy.raw.Exception if it cannot.")
		.def("load_action", [](py::object self, const std::string& filename) {
			auto& mw = inRack(self, "load_action");
			unlocked([&] { mw.loadAction(filename); });
		}, py::arg("filename"), "load() with an undo step; Rack reports a failure in a dialog.")
		.def("load_template", [](py::object self) {
			auto& mw = withModel(self, "load_template");
			withModule(self, "load_template");
			unlocked([&] { mw.loadTemplate(); });
		})
		.def("load_dialog", [](py::object self) {
			auto& mw = inRack(self, "load_dialog");
			unlocked([&] { mw.loadDialog(); });
		}, "Rack's: opens a file dialog and waits for the user.")
		.def("save", [](py::object self, const std::string& filename) {
			auto& mw = withModule(self, "save");
			unlocked([&] { mw.save(filename); });
		}, py::arg("filename"), "Rack's: saves a .vcvm preset.")
		.def("save_template", [](py::object self) {
			auto& mw = withModel(self, "save_template");
			withModule(self, "save_template");
			unlocked([&] { mw.saveTemplate(); });
		})
		.def("save_template_dialog", [](py::object self) {
			auto& mw = inRack(self, "save_template_dialog");
			unlocked([&] { mw.saveTemplateDialog(); });
		}, "Rack's: asks before overwriting an existing template, in a dialog that waits for the user.")
		.def("has_template", [](py::object self) { return withModel(self, "has_template").hasTemplate(); })
		.def("clear_template", [](py::object self) { withModel(self, "clear_template").clearTemplate(); })
		.def("clear_template_dialog", [](py::object self) {
			auto& mw = withModel(self, "clear_template_dialog");
			unlocked([&] { mw.clearTemplateDialog(); });
		}, "Rack's: asks in a dialog that waits for the user.")
		.def("save_dialog", [](py::object self) {
			auto& mw = inRack(self, "save_dialog");
			unlocked([&] { mw.saveDialog(); });
		}, "Rack's: opens a file dialog and waits for the user.")
		.def("disconnect", [](py::object self) {
			checkMutation();
			auto& mw = inRack(self, "disconnect");
			unlocked([&] { mw.disconnect(); });
		}, "Rack's: removes every cable on the widget's ports, without an undo step.")
		.def("reset_action", [](py::object self) {
			auto& mw = inRack(self, "reset_action");
			unlocked([&] { mw.resetAction(); });
		}, "Rack's Initialize, with an undo step.")
		.def("randomize_action", [](py::object self) {
			auto& mw = inRack(self, "randomize_action");
			unlocked([&] { mw.randomizeAction(); });
		}, "Rack's Randomize, with an undo step.")
		.def("disconnect_action", [](py::object self) {
			checkMutation();
			auto& mw = inRack(self, "disconnect_action");
			unlocked([&] { mw.disconnectAction(); });
		}, "Rack's Disconnect cables, with an undo step.")
		.def("clone_action", [](py::object self, bool cloneCables) {
			checkMutation();
			auto& mw = inRack(self, "clone_action");
			unlocked([&] { mw.cloneAction(cloneCables); });
		}, py::arg("clone_cables") = true, "Rack's Duplicate (with cables, or without), with an undo step.")
		.def("bypass_action", [](py::object self, bool bypassed) {
			auto& mw = inRack(self, "bypass_action");
			unlocked([&] { mw.bypassAction(bypassed); });
		}, py::arg("bypassed"), "Rack's Bypass, with an undo step.")
		.def("remove_action", [](py::object self) {
			checkMutation();
			auto& mw = inRack(self, "remove_action");
			unlocked([&] { mw.removeAction(); });
		}, "Rack's Delete, with an undo step: deletes this widget and its module, so this object is stale afterwards. "
			"Refused inside a widget callback.")
		.def("create_context_menu", [](py::object self) {
			checkMutation();
			auto& mw = inRack(self, "create_context_menu");
			CallbackGuard guard;
			mw.createContextMenu();
		}, "Rack's module context menu, then append_context_menu's items, opened in Rack's scene.")
		.def("get_grid_position", [](py::object self) { return native<MW>(self).getGridPosition(); })
		.def("set_grid_position", [](py::object self, rack::math::Vec pos) { native<MW>(self).setGridPosition(pos); },
			py::arg("pos"), "Rack's: moves the widget to rack grid cell pos (no collision checks).")
		.def("get_grid_size", [](py::object self) { return native<MW>(self).getGridSize(); })
		.def("get_grid_box", [](py::object self) { return native<MW>(self).getGridBox(); });
}

}  // namespace appdetail

std::unique_ptr<plugin::ModuleWidgetFactory> makeWidgetFactory(py::object fn) {
	if (!PyCallable_Check(fn.ptr()))
		throw py::type_error("widget_factory must be callable: a ModuleWidget subclass, or a function of the module");
	auto factory = std::make_unique<appdetail::PyWidgetFactory>();
	factory->fn = runtime::PyRef(fn);
	return factory;
}

py::object createModuleWidgetForPython(plugin::ModuleWidgetFactory& factory, rack::plugin::Model& model, py::object module) {
	auto* python = dynamic_cast<appdetail::PyWidgetFactory*>(&factory);
	if (!python || !python->fn)
		throw std::runtime_error("this model's widget factory is not available");
	rack::engine::Module* m = nullptr;
	if (!module.is_none()) {
		if (!py::isinstance<bindings::ModuleRef>(module))
			throw py::type_error("module must be a rackpy.raw.engine.Module or None");
		m = &module.cast<const bindings::ModuleRef&>().get();
		if (m->model != &model)
			throw std::invalid_argument("the module is not of this model");
		if (!dynamic_cast<module::Module*>(m)->engineOwned.load(std::memory_order_acquire))
			throw runtime::Ownership("a module widget owns its module, which must be in Rack's engine");
		auto* context = rack::contextGet();
		if (context && context->scene && context->scene->rack && context->scene->rack->getModule(m->id))
			throw runtime::Ownership("this module already has its module widget in Rack's rack");
		if (appdetail::ownedElsewhere(m, nullptr))
			throw runtime::Ownership("another module widget already owns this module");
	}
	return appdetail::build(python->fn.get(), model, m);
}

}  // namespace widgets
}  // namespace rackpy
