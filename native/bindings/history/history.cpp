// SPDX-License-Identifier: GPL-3.0-or-later
// rack::history -> rackpy.raw.history (RP-24C).
//
// Ownership: Rack's history State owns the actions pushed to it, and a
// ComplexAction owns its children; both delete them (State::push drops the
// actions after the current one, State::clear all of them). Every action
// Python makes is the trampoline (py::init_alias): pushed, its native object
// goes to Rack and keeps its Python half alive, and when Rack deletes it the
// Python object is left without one (pybind11 raises on use). An action Rack
// made is never handed out as a bound object: Python sees it as an
// ActionView, which finds it again in its tree at every use.
//
// Rack's own undo and redo assume their IDs are good: ParamChange and CableAdd
// index ports and params unchecked, ModuleAdd needs its model, and CableAdd
// adds the engine cable before the cable widget throws for a missing port.
// The trampolines check those first and otherwise do nothing, as Rack does for
// a missing module.
#include "history.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/trampoline_self_life_support.h>

#include <cxxabi.h>

#include <deque>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include "../app/app.hpp"
#include "../plugin.hpp"
#include "../widget/managed.hpp"
#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/json.hpp"
#include "rackpy/runtime/python.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;
namespace h = rack::history;

namespace {

int running = 0;  // main thread: actions whose undo or redo is running

struct Running {
	Running() { ++running; }
	~Running() { --running; }
	Running(const Running&) = delete;
	Running& operator=(const Running&) = delete;
};

/** Every action Python made. */
struct PyAction {
	bool ownedByRack = false;
	virtual ~PyAction() = default;
	virtual PyObject* pythonObject() const noexcept = 0;
};

rack::Context* liveApp() {
	auto* c = rack::contextGet();
	return c && c->engine && c->scene && c->scene->rack ? c : nullptr;
}

// What Rack's own undo and redo need, checked first (`redo`: the direction Rack runs).
bool runnable(const h::Action&, bool) { return true; }
bool creatable(const h::ModuleAdd& a) { return a.model && a.moduleJ; }  // Rack: model->createModule(), fromJson
bool runnable(const h::ModuleAdd& a, bool redo) { return !redo || creatable(a); }
bool runnable(const h::ModuleRemove& a, bool redo) { return redo || creatable(a); }
bool runnable(const h::ModuleChange& a, bool redo) { return redo ? a.newModuleJ != nullptr : a.oldModuleJ != nullptr; }
bool runnable(const h::ParamChange& a, bool) {
	auto* c = liveApp();
	if (!c)
		return false;
	auto* m = c->engine->getModule(a.moduleId);
	return !m || (a.paramId >= 0 && static_cast<size_t>(a.paramId) < m->params.size());
}
bool cableCreatable(const h::CableAdd& a) {
	auto* c = liveApp();
	if (!c)
		return false;
	auto* in = c->engine->getModule(a.inputModuleId);
	auto* out = c->engine->getModule(a.outputModuleId);
	if (!in || !out)
		return true;  // Rack does nothing
	if (a.inputId < 0 || static_cast<size_t>(a.inputId) >= in->inputs.size() || a.outputId < 0 ||
		static_cast<size_t>(a.outputId) >= out->outputs.size())
		return false;
	auto* inWidget = c->scene->rack->getModule(a.inputModuleId);
	auto* outWidget = c->scene->rack->getModule(a.outputModuleId);
	if (!inWidget || !outWidget)
		return false;
	auto* input = inWidget->getInput(a.inputId);
	auto* output = outWidget->getOutput(a.outputId);
	return input && output && widgets::portCableSafe(*input) && widgets::portCableSafe(*output);
}
bool runnable(const h::CableAdd& a, bool redo) { return !redo || cableCreatable(a); }
bool runnable(const h::CableRemove& a, bool redo) { return redo || cableCreatable(a); }

/** The trampoline for every action type. `Base` is the most-derived bound type. */
template <class Base>
struct ActionT : Base, PyAction, py::trampoline_self_life_support {
	using Base::Base;
	~ActionT() override {
		if (ownedByRack)
			runtime::noteTransferredDeleted();
	}
	PyObject* pythonObject() const noexcept override { return reinterpret_cast<PyObject*>(this->v_h.inst); }
	void undo() override { run("undo", false); }
	void redo() override { run("redo", true); }

	/** Rack's code for this type, checked, without the GIL. */
	void native(bool redo) {
		if (!runnable(static_cast<const Base&>(*this), redo))
			return;
		auto call = [&] { redo ? Base::redo() : Base::undo(); };
		if (runtime::interpreterAlive() && PyGILState_Check()) {
			py::gil_scoped_release release;
			call();
		}
		else
			call();
	}
	void run(const char* name, bool redo) {
		Running guard;
		if (!runtime::interpreterAlive()) {
			native(redo);
			return;
		}
		const Base* self = this;
		if (PyGILState_Check()) {  // from Python: an override's errors reach the caller
			if (py::function f = py::get_override(self, name)) {
				f();
				return;
			}
			native(redo);
			return;
		}
		// From Rack (its undo menu or keys): errors are reported, and a failed override is not followed by Rack's code.
		bool overridden = false;
		runtime::callPython(name, [&] {
			if (py::function f = py::get_override(self, name)) {
				overridden = true;
				f();
			}
		});
		if (!overridden)
			native(redo);
	}
};

PyAction* pyAction(const h::Action* a) {
	return dynamic_cast<PyAction*>(const_cast<h::Action*>(a));
}

/** Python's half of an action, or nothing for one Rack made. */
py::object pythonObjectOf(const h::Action* a) {
	if (auto* p = pyAction(a))
		if (PyObject* o = p->pythonObject())
			return py::reinterpret_borrow<py::object>(o);
	return py::object();
}

std::string typeName(const h::Action* a) {
	int status = 0;
	const char* mangled = typeid(*a).name();
	std::unique_ptr<char, void (*)(void*)> name(abi::__cxa_demangle(mangled, nullptr, nullptr, &status), std::free);
	return status == 0 && name ? name.get() : mangled;
}

template <class Seq>
bool contains(const Seq& actions, const h::Action* target) {
	for (const h::Action* a : actions) {
		if (a == target)
			return true;
		if (auto* c = dynamic_cast<const h::ComplexAction*>(a); c && contains(c->actions, target))
			return true;
	}
	return false;
}

/** An action Rack made, found again in its tree (Rack's history, or a ComplexAction Python owns) at every use. */
struct ActionView {
	const h::Action* action;
	std::shared_ptr<runtime::ContextToken> token;  // Rack's history, when root is None
	py::object root = py::none();                    // a Python ComplexAction

	const h::Action* find() const {
		runtime::requireMainThread("using a history action");
		if (root.is_none()) {
			if (!token->alive() || !token->context->history)
				return nullptr;
			return contains(token->context->history->actions, action) ? action : nullptr;
		}
		try {
			auto& c = root.cast<h::ComplexAction&>();
			return contains(c.actions, action) ? action : nullptr;
		}
		catch (const std::exception&) {
			return nullptr;  // Python's ComplexAction went to Rack, or was deleted
		}
	}
	const h::Action& get() const {
		const h::Action* a = find();
		if (!a)
			throw runtime::StaleHandle("Rack deleted this history action");
		return *a;
	}
};

py::object actionObject(const h::Action* a, const std::shared_ptr<runtime::ContextToken>& token, py::object root) {
	py::object own = pythonObjectOf(a);
	if (own)
		return own;
	return py::cast(ActionView{a, token, std::move(root)});
}

template <class Seq>
py::list actionList(const Seq& actions, const std::shared_ptr<runtime::ContextToken>& token, py::object root) {
	py::list out;
	for (const h::Action* a : actions)
		out.append(actionObject(a, token, root));
	return out;
}

/** Python gives Rack (a State or a ComplexAction) an action it owns. */
h::Action* takeAction(py::object value, const char* what) {
	runtime::requireMainThread(what);
	if (!py::isinstance<h::Action>(value))
		throw py::type_error(std::string(what) + " takes a rackpy.raw.history.Action");
	auto* existing = value.cast<h::Action*>();
	auto* p = pyAction(existing);
	if (!p)
		throw runtime::Ownership("Rack made this action");
	if (p->ownedByRack)
		throw runtime::Ownership("Rack already owns this action (in its history or a ComplexAction)");
	std::unique_ptr<h::Action> owned;
	try {
		owned = value.cast<std::unique_ptr<h::Action>>();
	}
	catch (const std::exception& e) {
		throw runtime::Ownership(std::string("Python cannot give this action away: ") + e.what());
	}
	p->ownedByRack = true;
	runtime::noteTransferred();
	return owned.release();
}

std::shared_ptr<runtime::ContextToken> liveToken(const char* what) {
	auto token = runtime::binding().current();
	if (!token || rack::contextGet() != token->context || !liveApp())
		throw runtime::WrongContext(std::string(what) + " needs Rack's live bound UI context");
	return token;
}

/** A bound undo or redo from Python. */
template <class T>
void pythonRun(T& a, bool redo) {
	runtime::requireMainThread(redo ? "redoing an action" : "undoing an action");
	liveToken(redo ? "redo" : "undo");
	runtime::refuseEngineReentry(redo ? "redo" : "undo");
	redo ? a.redo() : a.undo();
}

/** Rack's own code for the type, checked: for InverseAction and Python overrides that extend Rack's. */
template <class T>
void rackRun(T& a, bool redo) {
	runtime::requireMainThread("running an action");
	liveToken(redo ? "redo" : "undo");
	runtime::refuseEngineReentry(redo ? "redo" : "undo");
	Running guard;
	if (!runnable(a, redo))
		return;
	py::gil_scoped_release release;
	redo ? a.T::redo() : a.T::undo();
}

template <class T, class Cls>
void actionMethods(Cls& cls) {
	cls.def(py::init_alias<>())
		.def("undo", [](T& a) { pythonRun(a, false); }, "Rack's undo for this action (checked), or a Python override.")
		.def("redo", [](T& a) { pythonRun(a, true); })
		.def("_rack_undo", [](T& a) { rackRun(a, false); }, "Rack's own undo for this type, never a Python override.")
		.def("_rack_redo", [](T& a) { rackRun(a, true); })
		.def_property_readonly("owned_by_rack", [](const T& a) {
			auto* p = pyAction(&a);
			return p && p->ownedByRack;
		}, "True once Rack's history or a ComplexAction owns it; Rack may then delete it.");
}

template <class T, class V, class Cls>
void field(Cls& cls, const char* name, V T::*member, const char* doc = "") {
	cls.def_property(name,
		[member](const T& a) {
			runtime::requireMainThread("reading a history action");
			return a.*member;
		},
		[member](T& a, V value) {
			runtime::requireMainThread("editing a history action");
			a.*member = value;
		},
		doc);
}

template <class T, class Cls>
void jsonField(Cls& cls, const char* name, json_t* T::*member) {
	cls.def_property(name,
		[member](const T& a) -> py::object {
			runtime::requireMainThread("reading a history action");
			return a.*member ? runtime::fromJson(a.*member) : py::none();
		},
		[member](T& a, py::handle value) {
			runtime::requireMainThread("editing a history action");
			json_t* json = value.is_none() ? nullptr : runtime::toJson(value);
			json_decref(a.*member);
			a.*member = json;
		},
		"Rack's module JSON (the action owns its copy), or None.");
}

rack::app::ModuleWidget& moduleWidgetArgument(py::handle value) {
	auto* mw = dynamic_cast<rack::app::ModuleWidget*>(widgets::argument(value));
	if (!mw)
		throw py::type_error("expected a module widget (rackpy.raw.app.ModuleWidget, or Rack's)");
	if (!mw->getModule() || !mw->getModel())
		throw std::invalid_argument("Rack needs the module widget's module and model");
	return *mw;
}

rack::app::CableWidget& cableWidgetArgument(py::handle value) {
	auto* cw = dynamic_cast<rack::app::CableWidget*>(widgets::argument(value));
	if (!cw)
		throw py::type_error("expected a cable widget (rackpy.raw.app.HostCableWidget)");
	return *cw;
}

/** Rack's history (Context.history), checked. */
struct HistoryHandle {
	std::shared_ptr<runtime::ContextToken> token;
	h::State& get() const {
		runtime::requireMainThread("using Rack's history");
		if (!token->alive())
			throw runtime::StaleHandle("Rack's context has closed");
		runtime::checkContext(*token);
		if (!token->context->history)
			throw runtime::StaleHandle("Rack's history has been deleted");
		return *token->context->history;
	}
};

/** State::undo and redo, as Rack's Edit menu calls them: Python actions' errors are reported, not raised. */
void runHistory(const HistoryHandle& handle, bool redo) {
	h::State& state = handle.get();
	refuseInAction(redo ? "redo" : "undo");
	liveToken(redo ? "redo" : "undo");
	runtime::refuseEngineReentry(redo ? "redo" : "undo");
	Running guard;
	py::gil_scoped_release release;
	redo ? state.redo() : state.undo();
}

}  // namespace

void refuseInAction(const char* what) {
	if (running > 0)
		throw runtime::Ownership(std::string(what) + " is refused while a history action's undo or redo runs: Rack's "
			"history would delete the running action");
}

rack::history::ComplexAction& ownedComplexAction(py::handle value) {
	runtime::requireMainThread("filling a ComplexAction");
	if (!py::isinstance<h::ComplexAction>(value))
		throw py::type_error("expected a rackpy.raw.history.ComplexAction");
	auto& c = value.cast<h::ComplexAction&>();
	auto* p = pyAction(&c);
	if (p && p->ownedByRack)
		throw runtime::Ownership("Rack owns this ComplexAction; fill it before pushing it");
	refuseInAction("filling a ComplexAction");
	return c;
}

py::object adoptComplexAction(rack::history::ComplexAction* action) {
	// Rack's object is plain: move its children into a Python-made ComplexAction, which Python owns.
	std::unique_ptr<h::ComplexAction> made(action);
	py::object result = py::module_::import("rackpy.raw.history").attr("ComplexAction")();
	auto& target = result.cast<h::ComplexAction&>();
	target.name = made->name;
	target.actions.swap(made->actions);
	return result;
}

py::object historyObject(const std::shared_ptr<runtime::ContextToken>& token) {
	if (!token->context->history)
		return py::none();
	return runtime::identity(token->context->history, token->generation, [&] { return py::cast(HistoryHandle{token}); });
}

void bindHistory(py::module_ m) {
	py::class_<ActionView>(m, "ActionView", py::is_final(),
		"An action Rack made, in Rack's history or a ComplexAction: found again in its tree at every use, read-only.")
		.def_property_readonly("alive", [](const ActionView& v) { return v.find() != nullptr; })
		.def_property_readonly("name", [](const ActionView& v) { return v.get().name; })
		.def_property_readonly("type_name", [](const ActionView& v) { return typeName(&v.get()); },
			"The action's C++ type (rack::history::ModuleMove, or a plugin's).")
		.def_property_readonly("module_id", [](const ActionView& v) -> py::object {
			if (auto* a = dynamic_cast<const h::ModuleAction*>(&v.get()))
				return py::int_(a->moduleId);
			return py::none();
		}, "A ModuleAction's module ID, else None.")
		.def_property_readonly("actions", [](const ActionView& v) -> py::object {
			auto* c = dynamic_cast<const h::ComplexAction*>(&v.get());
			if (!c)
				return py::none();
			return actionList(c->actions, v.token, v.root);
		}, "A ComplexAction's children, else None.");

	py::classh<h::Action, ActionT<h::Action>> action(m, "Action",
		"Rack's undo action, with its inverse redo. Subclass it (or ModuleAction) and override undo and redo; push it "
		"to Context.history. Actions hold IDs, never objects, since Rack deletes and restores those.");
	actionMethods<h::Action>(action);
	field(action, "name", &h::Action::name, "Rack's: lower case, for \"Undo <name>\".");

	py::classh<h::ComplexAction, h::Action, ActionT<h::ComplexAction>> complex(m, "ComplexAction",
		"Rack's batch of actions, undone in reverse order. It owns its children.");
	actionMethods<h::ComplexAction>(complex);
	complex
		.def("push", [](py::object self, py::object child) {
			auto& c = ownedComplexAction(self);
			if (child.is(self))
				throw runtime::Ownership("a ComplexAction cannot contain itself");
			c.push(takeAction(child, "ComplexAction.push"));
		}, py::arg("action"), "Rack's: appends an action Python owns; this ComplexAction owns it from now on.")
		.def("is_empty", [](h::ComplexAction& c) {
			runtime::requireMainThread("reading a history action");
			return c.isEmpty();
		})
		.def_property_readonly("actions", [](py::object self) {
			runtime::requireMainThread("reading a history action");
			auto& c = self.cast<h::ComplexAction&>();
			auto token = runtime::binding().current();
			return actionList(c.actions, token, self);
		}, "Rack's: the children, in order: Python's own objects, or ActionViews of those Rack made.");

	py::classh<h::ModuleAction, h::Action, ActionT<h::ModuleAction>> moduleAction(m, "ModuleAction",
		"Rack's action on one module, by ID. Subclass it for a module's own undo steps.");
	actionMethods<h::ModuleAction>(moduleAction);
	field(moduleAction, "module_id", &h::ModuleAction::moduleId);

	py::classh<h::ModuleAdd, h::ModuleAction, ActionT<h::ModuleAdd>> moduleAdd(m, "ModuleAdd",
		"Rack's: undo deletes the module's widget; redo creates the module from its model and JSON, with its widget at "
		"pos. Redo needs the model and module_j.");
	actionMethods<h::ModuleAdd>(moduleAdd);
	moduleAdd
		.def_property("model", [](const h::ModuleAdd& a) -> py::object {
				runtime::requireMainThread("reading a history action");
				return a.model ? modelObject(a.model) : py::none();
			}, [](h::ModuleAdd& a, py::object model) {
				runtime::requireMainThread("editing a history action");
				a.model = model.is_none() ? nullptr : modelArgument(model);
			})
		.def("set_module", [](h::ModuleAdd& a, py::object mwObject) {
			runtime::requireMainThread("ModuleAdd.set_module");
			auto& mw = moduleWidgetArgument(mwObject);
			liveToken("ModuleAdd.set_module");
			runtime::refuseEngineReentry("ModuleAdd.set_module");
			bool present;
			{
				py::gil_scoped_release release;
				present = liveApp()->engine->hasModule(mw.getModule());
			}
			if (!present)
				throw runtime::Ownership("the module widget's module is not in Rack's engine");
			json_t* old = a.moduleJ;
			{
				py::gil_scoped_release release;  // Rack's moduleToJson takes the engine lock
				a.setModule(&mw);
			}
			if (old != a.moduleJ)
				json_decref(old);  // Rack's setModule overwrites it
		}, py::arg("module_widget"), "Rack's: the widget's model, module ID, position and module JSON.");
	field(moduleAdd, "pos", &h::ModuleAdd::pos);
	jsonField(moduleAdd, "module_j", &h::ModuleAdd::moduleJ);

	py::classh<h::ModuleRemove, h::ModuleAdd, ActionT<h::ModuleRemove>> moduleRemove(m, "ModuleRemove",
		"Rack's InverseAction<ModuleAdd>: undo creates the module, redo deletes it.");
	actionMethods<h::ModuleRemove>(moduleRemove);

	py::classh<h::ModuleMove, h::ModuleAction, ActionT<h::ModuleMove>> moduleMove(m, "ModuleMove");
	actionMethods<h::ModuleMove>(moduleMove);
	field(moduleMove, "old_pos", &h::ModuleMove::oldPos);
	field(moduleMove, "new_pos", &h::ModuleMove::newPos);

	py::classh<h::ModuleBypass, h::ModuleAction, ActionT<h::ModuleBypass>> moduleBypass(m, "ModuleBypass");
	actionMethods<h::ModuleBypass>(moduleBypass);
	field(moduleBypass, "bypassed", &h::ModuleBypass::bypassed, "Rack's: the state redo sets; undo sets the other.");

	py::classh<h::ModuleChange, h::ModuleAction, ActionT<h::ModuleChange>> moduleChange(m, "ModuleChange",
		"Rack's: loads the module's old or new JSON.");
	actionMethods<h::ModuleChange>(moduleChange);
	jsonField(moduleChange, "old_module_j", &h::ModuleChange::oldModuleJ);
	jsonField(moduleChange, "new_module_j", &h::ModuleChange::newModuleJ);

	py::classh<h::ParamChange, h::ModuleAction, ActionT<h::ParamChange>> paramChange(m, "ParamChange",
		"Rack's: sets the param's old or new value. The param ID is checked (Rack's is not).");
	actionMethods<h::ParamChange>(paramChange);
	field(paramChange, "param_id", &h::ParamChange::paramId);
	field(paramChange, "old_value", &h::ParamChange::oldValue);
	field(paramChange, "new_value", &h::ParamChange::newValue);

	py::classh<h::CableAdd, h::Action, ActionT<h::CableAdd>> cableAdd(m, "CableAdd",
		"Rack's: undo deletes the cable's widget; redo creates the cable and its widget. Redo needs the ports' widgets "
		"(checked: Rack would leave an engine cable behind and raise).");
	actionMethods<h::CableAdd>(cableAdd);
	field(cableAdd, "cable_id", &h::CableAdd::cableId);
	field(cableAdd, "input_module_id", &h::CableAdd::inputModuleId);
	field(cableAdd, "input_id", &h::CableAdd::inputId);
	field(cableAdd, "output_module_id", &h::CableAdd::outputModuleId);
	field(cableAdd, "output_id", &h::CableAdd::outputId);
	field(cableAdd, "color", &h::CableAdd::color);
	cableAdd
		.def("set_cable", [](h::CableAdd& a, py::object cwObject) {
			runtime::requireMainThread("CableAdd.set_cable");
			auto& cw = cableWidgetArgument(cwObject);
			if (!cw.cable || cw.cable->id < 0 || !cw.cable->inputModule || !cw.cable->outputModule)
				throw std::invalid_argument("Rack needs a complete cable widget whose cable is in the engine");
			a.setCable(&cw);
		}, py::arg("cable_widget"), "Rack's: the cable's IDs, ports and colour.")
		.def("is_cable", [](const h::CableAdd& a, py::object cwObject) {
			runtime::requireMainThread("CableAdd.is_cable");
			if (cwObject.is_none())
				return a.isCable(nullptr);
			return a.isCable(&cableWidgetArgument(cwObject));
		}, py::arg("cable_widget"), "Rack's: whether the cable widget joins the same ports.");

	py::classh<h::CableRemove, h::CableAdd, ActionT<h::CableRemove>> cableRemove(m, "CableRemove",
		"Rack's InverseAction<CableAdd>: undo creates the cable, redo deletes it.");
	actionMethods<h::CableRemove>(cableRemove);

	py::classh<h::CableColorChange, h::Action, ActionT<h::CableColorChange>> colorChange(m, "CableColorChange");
	actionMethods<h::CableColorChange>(colorChange);
	field(colorChange, "cable_id", &h::CableColorChange::cableId);
	field(colorChange, "new_color", &h::CableColorChange::newColor);
	field(colorChange, "old_color", &h::CableColorChange::oldColor);
	colorChange.def("set_cable", [](h::CableColorChange& a, py::object cwObject) {
		runtime::requireMainThread("CableColorChange.set_cable");
		auto& cw = cableWidgetArgument(cwObject);
		if (!cw.cable || cw.cable->id < 0)
			throw std::invalid_argument("Rack needs a cable widget whose cable is in the engine");
		a.setCable(&cw);
	}, py::arg("cable_widget"), "Rack's: the cable's ID.");

	py::class_<HistoryHandle>(m, "State", py::is_final(),
		"Rack's undo history (Context.history). Main thread. It owns the actions pushed to it, and deletes those after "
		"the current one when another is pushed.")
		.def_property_readonly("alive", [](const HistoryHandle& handle) {
			return handle.token->alive() && handle.token->context->history;
		})
		.def("push", [](const HistoryHandle& handle, py::object action) {
			h::State& state = handle.get();
			refuseInAction("push");
			state.push(takeAction(action, "State.push"));
		}, py::arg("action"),
			"Rack's: adds an action Python owns, without running it (do it first, then push it). Rack owns it from now "
			"on, and deletes the actions that were undone.")
		.def("undo", [](const HistoryHandle& handle) { runHistory(handle, false); },
			"Rack's Undo. Errors in Python actions are reported, as from Rack's Edit menu.")
		.def("redo", [](const HistoryHandle& handle) { runHistory(handle, true); })
		.def("can_undo", [](const HistoryHandle& handle) { return handle.get().canUndo(); })
		.def("can_redo", [](const HistoryHandle& handle) { return handle.get().canRedo(); })
		.def("get_undo_name", [](const HistoryHandle& handle) { return handle.get().getUndoName(); })
		.def("get_redo_name", [](const HistoryHandle& handle) { return handle.get().getRedoName(); })
		.def("set_saved", [](const HistoryHandle& handle) { handle.get().setSaved(); },
			"Rack's: the current state is the saved patch's.")
		.def("is_saved", [](const HistoryHandle& handle) { return handle.get().isSaved(); })
		.def_property_readonly("actions", [](const HistoryHandle& handle) {
			h::State& state = handle.get();
			return actionList(state.actions, handle.token, py::none());
		}, "Rack's: every action, oldest first: Python's own objects, or ActionViews of those Rack made.")
		.def_property_readonly("action_index", [](const HistoryHandle& handle) { return handle.get().actionIndex; },
			"Rack's: how many actions are done (the next undo is the one before it). Read-only: Rack's own.")
		.def_property_readonly("saved_index", [](const HistoryHandle& handle) { return handle.get().savedIndex; },
			"Rack's: the action index of the saved patch, or -1. Read-only (set_saved sets it).");
}

}  // namespace bindings
}  // namespace rackpy
