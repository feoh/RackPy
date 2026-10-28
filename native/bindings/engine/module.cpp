// SPDX-License-Identifier: GPL-3.0-or-later
// RP-14A/B1/B2b5: scoped construction, components, built-in metadata views
// and metadata class factories. No Module virtual calls dispatch to Python.
// Python runs only synchronously during construction on the main thread,
// before publication: the constructor callback and class-factory constructors.
#include "module.hpp"

#include "expander.hpp"
#include "../dsp/kernel.hpp"
#include "../plugin.hpp"
#include "components.hpp"
#include "metadata.hpp"
#include "quantity.hpp"
#include "../support/hooks.hpp"
#include "../support/overrides.hpp"
#include "rackpy/runtime/json.hpp"
#include "../values/values.hpp"
#include "../../laboratory/laboratory.hpp"

#include <pybind11/stl.h>

#include <array>
#include <functional>
#include <vector>

#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace bindings {

py::object moduleObject(const std::shared_ptr<module::Construction>& state) {
	auto& m = state->get();
	return runtime::identity(&m, m.token->generation, [&] { return py::cast(ModuleRef{state}); });
}

py::object ownModuleField(rack::engine::Module* field, const std::shared_ptr<module::Construction>& state) {
	if (!field)
		return py::none();
	if (field != &state->get())
		throw runtime::StaleHandle("module metadata has an untracked module pointer");
	return moduleObject(state);
}

void setOwnModuleField(rack::engine::Module*& field, py::handle value, const std::shared_ptr<module::Construction>& state) {
	state->requireConstruction();
	if (value.is_none()) {
		field = nullptr;
		return;
	}
	if (!py::isinstance<ModuleRef>(value) || value.cast<const ModuleRef&>().state != state)
		throw py::value_error("a module's own metadata can point only at that module, or None");
	field = &state->get();
}

std::shared_ptr<module::Construction> lockOwner(const std::weak_ptr<module::Construction>& state) {
	auto held = state.lock();
	if (!held)
		throw runtime::StaleHandle("this metadata belonged to a deleted module");
	return held;
}

std::shared_ptr<module::Construction> lockOwner(const OwnerState& state) {
	auto held = state.lock();
	if (!held)
		throw runtime::StaleHandle("this metadata belonged to a deleted module");
	return held;
}

namespace {

enum class Kind { Param, Input, Output, Light, ParamQuantity, InputInfo, OutputInfo, LightInfo, BypassRoute };

/** A metadata view never owns the Module or the underlying metadata. Its
slot generation dies on replacement, independently of the Module lifetime. */
template <typename Info>
struct InfoView {
	Info* ptr;
	std::shared_ptr<runtime::ObjectToken> token;
	OwnerState state;
	Info& get() const {
		runtime::requireMainThread("using module metadata");
		if (!token->alive())
			throw runtime::StaleHandle("this metadata was replaced or its module was deleted");
		return *ptr;
	}
	py::object owner() const {
		return ownModuleField(get().module, lockOwner(state));
	}
};

using PortInfoView = InfoView<rack::engine::PortInfo>;
using LightInfoView = InfoView<rack::engine::LightInfo>;

template <typename Info>
py::object infoView(const ModuleRef& ref, Info* ptr, const std::shared_ptr<runtime::ObjectToken>& token) {
	if (!ptr)
		return py::none();
	if (py::object transferred = transferredObject(ptr))
		return transferred;
	return runtime::identity(ptr, token->generation, [&] { return py::cast(InfoView<Info>{ptr, token, ref.state}); });
}

template <typename Info>
auto bindInfoView(py::module_ engine, const char* name) {
	using View = InfoView<Info>;
	return py::class_<View>(engine, name, "Checked built-in metadata; not a standalone metadata class or a Python virtual trampoline.")
		.def_property_readonly("alive", [](const View& v) { return v.token->alive(); })
		.def_property("module", &View::owner, [](const View& v, py::handle value) {
			setOwnModuleField(v.get().module, value, lockOwner(v.state));
		}, "The owning module or None; assignable only during construction, to those two values.")
		.def_property("name", [](const View& v) { return v.get().name; },
			[](const View& v, std::string name) { v.get().name = std::move(name); })
		.def_property("description", [](const View& v) { return v.get().description; },
			[](const View& v, std::string description) { v.get().description = std::move(description); })
		.def("get_name", [](const View& v) { return v.get().getName(); })
		.def("get_description", [](const View& v) { return v.get().getDescription(); });
}

/** Each vector's own size: during construction a raw edit may leave a
component vector and its metadata vector different lengths until the seal. */
int count(const ModuleRef& ref, Kind kind) {
	module::Module& m = ref.get();
	switch (kind) {
		case Kind::Param: return m.getNumParams();
		case Kind::ParamQuantity: return static_cast<int>(m.paramQuantities.size());
		case Kind::Input: return m.getNumInputs();
		case Kind::InputInfo: return static_cast<int>(m.inputInfos.size());
		case Kind::Output: return m.getNumOutputs();
		case Kind::OutputInfo: return static_cast<int>(m.outputInfos.size());
		case Kind::Light: return m.getNumLights();
		case Kind::LightInfo: return static_cast<int>(m.lightInfos.size());
		case Kind::BypassRoute: return static_cast<int>(m.bypassRoutes.size());
	}
	throw std::logic_error("unknown component kind");
}

py::object component(const ModuleRef& ref, Kind kind, int i) {
	module::Module& m = ref.get();
	module::Construction::index(i, count(ref, kind));
	switch (kind) {
		case Kind::Param: return borrowParam(&m.getParam(i), m.paramsToken);
		case Kind::ParamQuantity: return quantityView(ref, i);
		case Kind::Input: return borrowInput(&m.getInput(i), m.inputsToken);
		case Kind::Output: return borrowOutput(&m.getOutput(i), m.outputsToken);
		case Kind::Light: return borrowLight(&m.getLight(i), m.lightsToken);
		case Kind::InputInfo: return infoView(ref, m.getInputInfo(i), m.inputInfoTokens[i]);
		case Kind::OutputInfo: return infoView(ref, m.getOutputInfo(i), m.outputInfoTokens[i]);
		case Kind::LightInfo: return infoView(ref, m.getLightInfo(i), m.lightInfoTokens[i]);
		case Kind::BypassRoute: return py::cast(m.bypassRoutes[i], py::return_value_policy::copy);  // a value
	}
	throw std::logic_error("unknown component kind");
}

/** Rack's raw vector edits (RP-14B2b7), by element type. Component vectors
hold values; a structural edit renews the vector's token, so every borrowed
view of that vector goes stale, like C++ iterator invalidation. Metadata
vectors hold owning pointers with one generation per slot. */
template <typename T>
struct ValueVector {
	std::vector<T>& values;
	std::shared_ptr<runtime::ObjectToken>& token;
	T (*convert)(py::handle);

	void set(size_t i, py::handle value) { values[i] = convert(value); }
	// std::vector's strong guarantee: a failed (reallocating) insert or
	// resize changes nothing, so the old views stay valid until we renew.
	void insert(size_t i, py::handle value) {
		T copy = convert(value);
		auto next = std::make_shared<runtime::ObjectToken>(nullptr);
		values.insert(values.begin() + i, copy);
		module::Construction::renew(token, std::move(next));
	}
	void erase(size_t i) {
		auto next = std::make_shared<runtime::ObjectToken>(nullptr);
		values.erase(values.begin() + i);
		module::Construction::renew(token, std::move(next));
	}
	void resize(size_t n) {
		if (n == values.size())
			return;
		auto next = std::make_shared<runtime::ObjectToken>(nullptr);
		values.resize(n);
		module::Construction::renew(token, std::move(next));
	}
};

template <typename Info>
struct SlotVector {
	std::vector<Info*>& infos;
	module::Module::Tokens& tokens;
	std::function<std::unique_ptr<Info>(py::handle)> take;  // None -> null

	void set(size_t i, py::handle value) {
		auto generation = std::make_shared<runtime::ObjectToken>(nullptr);
		auto next = take(value);  // commits a transfer: nothing below throws
		module::Construction::setSlot(infos, tokens, i, next.release(), std::move(generation));
	}
	void insert(size_t i, py::handle value) {
		module::Construction::reserveSlots(infos, tokens, infos.size() + 1);
		auto generation = std::make_shared<runtime::ObjectToken>(nullptr);
		auto next = take(value);
		module::Construction::insertSlot(infos, tokens, i, next.release(), std::move(generation));
	}
	void erase(size_t i) { module::Construction::eraseSlot(infos, tokens, i); }
	void resize(size_t n) {
		auto added = module::Module::tokens(n > infos.size() ? n - infos.size() : 0);
		module::Construction::reserveSlots(infos, tokens, n);
		module::Construction::resizeSlots(infos, tokens, n, std::move(added));
	}
};

rack::engine::Module::BypassRoute routeValue(py::handle value) {
	if (!py::isinstance<rack::engine::Module::BypassRoute>(value))
		throw py::type_error("a module's bypass routes take Module.BypassRoute values");
	return value.cast<rack::engine::Module::BypassRoute>();
}

/** Raw vector assignment of a Python-created PortInfo/LightInfo, or None.
Unlike a class factory it sets no fields; the seal checks them. */
template <typename Info>
std::unique_ptr<Info> takeInfo(const ModuleRef& ref, py::handle value) {
	if (value.is_none())
		return nullptr;
	if (!py::isinstance<Info>(value))
		throw py::type_error("a module's metadata vector takes a " + std::string(py::str(py::type::of<Info>().attr("__name__"))) +
			" you created, or None");
	return disownForSlot<Info>(py::reinterpret_borrow<py::object>(value), infoAlias(value.cast<Info&>()), ref.state, false);
}

template <typename F>
void editVector(const ModuleRef& ref, Kind kind, F edit) {
	module::Module& m = ref.get();
	switch (kind) {
		case Kind::Param: return edit(ValueVector<rack::engine::Param>{m.params, m.paramsToken, &paramValue});
		case Kind::Input: return edit(ValueVector<rack::engine::Input>{m.inputs, m.inputsToken, &inputValue});
		case Kind::Output: return edit(ValueVector<rack::engine::Output>{m.outputs, m.outputsToken, &outputValue});
		case Kind::Light: return edit(ValueVector<rack::engine::Light>{m.lights, m.lightsToken, &lightValue});
		case Kind::ParamQuantity:
			return edit(SlotVector<rack::engine::ParamQuantity>{m.paramQuantities, m.paramQuantityTokens,
				[&ref](py::handle v) { return takeQuantity(ref, v); }});
		case Kind::InputInfo:
			return edit(SlotVector<rack::engine::PortInfo>{m.inputInfos, m.inputInfoTokens,
				[&ref](py::handle v) { return takeInfo<rack::engine::PortInfo>(ref, v); }});
		case Kind::OutputInfo:
			return edit(SlotVector<rack::engine::PortInfo>{m.outputInfos, m.outputInfoTokens,
				[&ref](py::handle v) { return takeInfo<rack::engine::PortInfo>(ref, v); }});
		case Kind::LightInfo:
			return edit(SlotVector<rack::engine::LightInfo>{m.lightInfos, m.lightInfoTokens,
				[&ref](py::handle v) { return takeInfo<rack::engine::LightInfo>(ref, v); }});
		case Kind::BypassRoute:
			return edit(ValueVector<rack::engine::Module::BypassRoute>{m.bypassRoutes, m.routesToken, &routeValue});
	}
}

/** Edits only inside the constructor callback: owning metadata slots, and the
bypass routes Rack's engine reads in processBypass. */
bool constructorOnly(Kind kind) {
	return kind == Kind::ParamQuantity || kind == Kind::InputInfo || kind == Kind::OutputInfo || kind == Kind::LightInfo ||
		kind == Kind::BypassRoute;
}

/** Collections do not keep a module alive. Individual component views have
their vector's ObjectToken, not the module's owning Construction. */
struct Components {
	OwnerState state;
	Kind kind;
	ModuleRef ref() const {
		runtime::requireMainThread("using a module component collection");
		auto held = state.lock();
		if (!held)
			throw runtime::StaleHandle("this component collection belonged to a deleted module");
		ModuleRef r{held};
		r.get();
		return r;
	}
	int size() const { return count(ref(), kind); }
	py::object item(int i) const {
		ModuleRef r = ref();
		if (i < 0)
			i += count(r, kind);
		return component(r, kind, i);
	}
	/** Element writes to a component vector are allowed after the seal; every
	other edit, and any metadata slot assignment, only during construction. */
	void set(long i, py::handle value) const {
		ModuleRef r = ref();
		if (constructorOnly(kind))
			r.state->requireRawEdit();
		size_t at = module::Construction::editIndex(i, count(r, kind));
		editVector(r, kind, [&](auto vector) { vector.set(at, value); });
	}
	void erase(long i) const {
		ModuleRef r = ref();
		r.state->requireRawEdit();
		size_t at = module::Construction::editIndex(i, count(r, kind));
		editVector(r, kind, [&](auto vector) { vector.erase(at); });
	}
	void insert(long i, py::handle value) const {
		ModuleRef r = ref();
		r.state->requireRawEdit();
		size_t at = module::Construction::insertIndex(i, count(r, kind));
		editVector(r, kind, [&](auto vector) { vector.insert(at, value); });
	}
	void resize(long n) const {
		ModuleRef r = ref();
		r.state->requireRawEdit();
		if (n < 0)
			throw std::invalid_argument("a module vector cannot have a negative size");
		editVector(r, kind, [&](auto vector) { vector.resize(static_cast<size_t>(n)); });
	}
};

/** Rack's `new TInfo` for configInput/configOutput/configLight<TInfo>, with a
Python class. The construction phase and slot ID are checked again after the
(reentrant) constructor, before anything is transferred. */
template <typename Info>
std::unique_ptr<Info> makeTransferred(const ModuleRef& ref, py::handle type, const char* base, int id,
	size_t (*slots)(module::Module&)) {
	py::object instance = constructForSlot<Info>(type, "info_type", base, [&] {
		ref.state->requireConstruction();
		module::Construction::index(id, slots(ref.get()));
	});
	return disownForSlot<Info>(instance, infoAlias(instance.cast<Info&>()), ref.state);
}

size_t inputSlots(module::Module& m) { return m.inputInfos.size(); }
size_t outputSlots(module::Module& m) { return m.outputInfos.size(); }
size_t lightSlots(module::Module& m) { return m.lightInfos.size(); }

py::object configPort(const ModuleRef& r, int i, std::string name, py::object type, bool input) {
	if (type.is_none())
		r.state->configPortInfo(i, std::move(name), input);
	else
		r.state->configPortInfo(i, std::move(name), input, [&] {
			return makeTransferred<rack::engine::PortInfo>(r, type, "PortInfo", i, input ? inputSlots : outputSlots);
		});
	return component(r, input ? Kind::InputInfo : Kind::OutputInfo, i);
}

struct Counts { int params, inputs, outputs, lights; };

py::object construct(std::shared_ptr<module::Construction> state, py::object configure, const Counts* counts) {
	runtime::requireMainThread("creating a RackPy module");
	if (!configure.is_none() && !PyCallable_Check(configure.ptr()))
		throw py::type_error("configure must be callable or None");
	state->object->construction = state;
	py::object result = moduleObject(state);
	try {
		// The deprecated counting constructor configures first, as Rack's does.
		if (counts)
			state->config(counts->params, counts->inputs, counts->outputs, counts->lights);
		if (!configure.is_none())
			configure(result);
		state->seal();
	}
	catch (...) {
		state->abort();
		throw;
	}
	return result;
}

py::object create(py::object configure) {
	return construct(std::make_shared<module::Construction>(), std::move(configure), nullptr);
}

py::object createConfigured(int params, int inputs, int outputs, int lights, py::object configure) {
	runtime::requireMainThread("creating a RackPy module");
	deprecated("Module(numParams, numInputs, numOutputs, numLights) is deprecated in Rack; call config() in the constructor callback");
	Counts counts{params, inputs, outputs, lights};
	return construct(std::make_shared<module::Construction>(), std::move(configure), &counts);
}

}  // namespace

py::object createWith(std::shared_ptr<module::Construction> state, py::object configure) {
	return construct(std::move(state), std::move(configure), nullptr);
}

rack::engine::Module* createForRack(py::object configure, rack::plugin::Model& model) {
	py::object handle = create(std::move(configure));
	const ModuleRef& r = handle.cast<const ModuleRef&>();
	r.get().model = &model;
	return r.state->releaseToRack();
}

py::object createOwned(py::object configure, rack::plugin::Model& model) {
	py::object handle = create(std::move(configure));
	handle.cast<const ModuleRef&>().get().model = &model;
	return handle;
}

py::object engineModuleObject(rack::engine::Module* module) {
	auto* m = dynamic_cast<module::Module*>(module);
	if (!m || !m->engineOwned.load(std::memory_order_acquire) || !m->token->alive())
		return py::none();
	return moduleObject(module::Construction::handleState(*m));
}

py::object liveModuleObject(module::Module& m) {
	if (!m.token->alive())
		return py::none();
	if (auto state = m.construction.lock())
		return moduleObject(state);
	if (m.engineOwned.load(std::memory_order_acquire))
		return moduleObject(module::Construction::handleState(m));
	return py::none();  // a Python-owned module lives only as long as its handles
}

using M = rack::engine::Module;

/** A Rack event as Python reads it: a copied value of Rack's own record. */
py::object eventObject(const module::LifecycleRecord& r) {
	using K = module::LifecycleKind;
	switch (r.kind) {
		case K::Add: return py::cast(M::AddEvent{});
		case K::Remove: return py::cast(M::RemoveEvent{});
		case K::Bypass: return py::cast(M::BypassEvent{});
		case K::UnBypass: return py::cast(M::UnBypassEvent{});
		case K::PortChange: return py::cast(M::PortChangeEvent{r.connecting, r.portType, r.portId});
		case K::Reset: return py::cast(M::ResetEvent{});
		case K::Randomize: return py::cast(M::RandomizeEvent{});
		case K::SampleRateChange: return py::cast(M::SampleRateChangeEvent{r.sampleRate, r.sampleTime});
		case K::Save: return py::cast(M::SaveEvent{});
		case K::SetMaster: return py::cast(M::SetMasterEvent{});
		case K::UnsetMaster: return py::cast(M::UnsetMasterEvent{});
		case K::ExpanderChange: {
			M::ExpanderChangeEvent e;
			e.side = r.side;
			return py::cast(e);
		}
	}
	throw std::logic_error("unknown lifecycle event");
}

/** Python calls a module's event handler directly, as C++ plugin code may:
Rack's behaviour runs on the main thread in the live bound context, without
the GIL, exactly as when Rack dispatches it. Rack dispatches the events of a
module its engine owns itself, under its engine lock. */
template <typename F>
void dispatch(const ModuleRef& r, F handler) {
	module::Module& m = r.get();
	if (r.state->constructing)
		throw runtime::Ownership("module events can be dispatched only after the constructor callback returns");
	if (m.engineOwned.load(std::memory_order_acquire))
		throw runtime::Ownership("Rack's engine dispatches the events of a module it owns");
	runtime::refuseEngineReentry("dispatching a module event");
	auto context = runtime::binding().current();
	if (!context || rack::contextGet() != context->context || !context->context->engine)
		throw runtime::WrongContext("module events need Rack's live bound UI context");
	if (m.laboratory) {
		// Laboratory callbacks are synchronous on the main thread. Retain the
		// GIL and propagate errors to the Python caller, as Bench does.
		ErrorCapture capture;
		runtime::EngineLease lease(*context);
		handler(m);
		capture.rethrow();
	}
	else {
		py::gil_scoped_release release;
		runtime::EngineLease lease(*context);
		handler(m);
	}
}

/** Python callables for Rack's dataToJson/dataFromJson (owner decision 2026-10-23). */
struct PyDataHandlers : module::DataHandlers {
	runtime::PyRef toJsonFn, fromJsonFn;
	json_t* toJson() noexcept override {
		json_t* result = nullptr;
		if (toJsonFn)
			runPythonHook("Module.data_to_json", [&] {
				py::object value = toJsonFn.get()();
				if (!value.is_none())
					result = runtime::toJson(value);
			});
		return result;
	}
	void fromJson(json_t* data) noexcept override {
		if (fromJsonFn)
			runPythonHook("Module.data_from_json", [&] { fromJsonFn.get()(runtime::fromJson(data)); });
	}
};

/** Python callables for the module's lifecycle events (RP-16C), called after
Rack's behaviour, on the main thread, contained. Rack's audio is stopped
during the ui-exclusive ones, so they are timed against ADR 004's 1 ms
budget. Nothing runs once Rack's context is closing: handles are stale then. */
struct PyEventHandlers : module::EventHandlers {
	std::array<runtime::PyRef, 16> handlers;
	static const char* name(module::LifecycleKind kind) {
		using K = module::LifecycleKind;
		switch (kind) {
			case K::Add: return "Module.on_add";
			case K::Remove: return "Module.on_remove";
			case K::Bypass: return "Module.on_bypass";
			case K::UnBypass: return "Module.on_un_bypass";
			case K::PortChange: return "Module.on_port_change";
			case K::Reset: return "Module.on_reset";
			case K::Randomize: return "Module.on_randomize";
			case K::Save: return "Module.on_save";
			default: return "Module event";
		}
	}
	void dispatch(const module::LifecycleRecord& record) noexcept override {
		const runtime::PyRef& handler = handlers[(size_t) record.kind % handlers.size()];
		if (!handler || !runtime::binding().current())
			return;
		bool exclusive = runtime::insideExclusive();
		double start = rack::system::getTime();
		runtime::callPython(name(record.kind), [&] { handler.get()(eventObject(record)); });
		double elapsed = rack::system::getTime() - start;
		if (exclusive && elapsed > 0.001) {
			std::string message = rack::string::f("%s took %.1f ms with Rack's audio stopped (budget 1 ms)",
				name(record.kind), elapsed * 1000.0);
			runtime::diagnose(runtime::DiagnosticCode::SlowExclusiveCallback, message.c_str());
		}
	}
};

using JsonPtr = std::unique_ptr<json_t, decltype(&json_decref)>;

/** Module JSON needs a sealed module (Rack's code assumes the seal's invariants). */
module::Module& sealedModule(const ModuleRef& r) {
	module::Module& m = r.get();
	if (r.state->constructing)
		throw runtime::Ownership("module JSON is available only after the constructor callback returns");
	return m;
}

std::shared_ptr<runtime::ContextToken> liveContext(const char* what) {
	auto context = runtime::binding().current();
	if (!context || rack::contextGet() != context->context || !context->context->engine)
		throw runtime::WrongContext(std::string(what) + " needs Rack's live bound UI context");
	return context;
}

/** Rack's parameter quantities read and write values through Rack's engine
(APP->engine), so module JSON needs the live context; a laboratory module
without parameters, which only a Bench runs, does not. */
bool jsonNeedsRack(const module::Module& m) {
	return !m.laboratory || !m.paramQuantities.empty();
}

/** Rack's Module JSON from Python. For a Python-owned module the call runs
here with the GIL, so a handler's or override's error reaches the caller.
For one Rack's engine owns, `viaEngine` takes the engine lock as Rack does,
with the GIL released; Python inside is then contained, as on a real save. */
template <typename Direct, typename ViaEngine>
auto moduleJson(const ModuleRef& r, const char* what, bool needsEngine, Direct direct, ViaEngine viaEngine) {
	module::Module& m = sealedModule(r);
	if (m.engineOwned.load(std::memory_order_acquire)) {
		runtime::refuseEngineReentry(what);
		auto context = liveContext(what);
		py::gil_scoped_release release;
		runtime::EngineLease lease(*context);
		return viaEngine(m);
	}
	if (needsEngine)
		liveContext(what);
	ErrorCapture capture;
	auto result = direct(m);
	capture.rethrow();
	return result;
}

/** A Python-owned module's handle owns, through native code, its data handlers
and its slots' transferred Python objects: report those edges to Python's
garbage collector, so cycles back to the handle (a handler closure, a
subclass attribute) are collected. A module Rack owns is not the handle's:
nothing is reported, and nothing is cleared. */
int traverseModule(PyObject* self, visitproc visit, void* arg) {
	Py_VISIT(Py_TYPE(self));
	auto& ref = py::cast<ModuleRef&>(py::handle(self));
	if (!ref.state || !ref.state->object)
		return 0;
	module::Module& m = *ref.state->object;
	if (auto* handlers = dynamic_cast<PyDataHandlers*>(m.dataHandlers.get())) {
		Py_VISIT(handlers->toJsonFn.borrowed());
		Py_VISIT(handlers->fromJsonFn.borrowed());
	}
	if (auto* handlers = dynamic_cast<PyEventHandlers*>(m.eventHandlers.get()))
		for (const auto& handler : handlers->handlers)
			Py_VISIT(handler.borrowed());
	if (auto* lab = laboratory::laboratoryModule(m)) {
		int failed = 0;
		lab->visit([&](PyObject* object) {
			if (object && !failed)
				failed = visit(object, arg);
		});
		if (failed)
			return failed;
	}
	for (auto* q : m.paramQuantities)
		if (auto* t = module::transferredIn(q))
			Py_VISIT(static_cast<PyObject*>(t->pythonObject()));
	for (auto* infos : {&m.inputInfos, &m.outputInfos})
		for (auto* info : *infos)
			if (auto* t = module::transferredIn(info))
				Py_VISIT(static_cast<PyObject*>(t->pythonObject()));
	for (auto* info : m.lightInfos)
		if (auto* t = module::transferredIn(info))
			Py_VISIT(static_cast<PyObject*>(t->pythonObject()));
	return 0;
}

int clearModule(PyObject* self) {
	auto& ref = py::cast<ModuleRef&>(py::handle(self));
	if (ref.state && ref.state->object) {
		ref.state->object->dataHandlers.reset();  // slot objects clear their own references
		ref.state->object->eventHandlers.reset();
		if (auto* lab = laboratory::laboratoryModule(*ref.state->object))
			lab->clearOverrides();
	}
	return 0;
}

template <typename Event>
void bindEvent(py::class_<ModuleRef>& cls, const char* name, const char* doc) {
	py::class_<Event>(cls, name, doc).def(py::init<>()).def("__repr__", [name](const Event&) {
		return std::string("Module.") + name + "()";
	});
}

void bindModule(py::module_ engine) {
	bindQuantityViews(engine);
	bindStandaloneQuantities(engine);
	bindInfoView<rack::engine::PortInfo>(engine, "PortInfoView")
		.def_property("port_id", [](const PortInfoView& v) { return v.get().portId; }, [](const PortInfoView& v, int id) {
			v.get();
			lockOwner(v.state)->requireConstruction();
			v.get().portId = id;
		})
		.def_property("type", [](const PortInfoView& v) { return v.get().type; }, [](const PortInfoView& v, rack::engine::Port::Type type) {
			v.get();
			lockOwner(v.state)->requireConstruction();
			v.get().type = type;
		})
		.def("get_full_name", [](const PortInfoView& v) { return v.get().getFullName(); });
	bindInfoView<rack::engine::LightInfo>(engine, "LightInfoView")
		.def_property("light_id", [](const LightInfoView& v) { return v.get().lightId; }, [](const LightInfoView& v, int id) {
			v.get();
			lockOwner(v.state)->requireConstruction();
			v.get().lightId = id;
		});
	py::class_<Components>(engine, "Components",
		"A checked view of one of a module's vectors; it does not keep its module alive. During construction "
		"it supports Rack's raw vector edits; afterwards only element writes to component vectors.")
		.def("__len__", &Components::size)
		.def("__getitem__", &Components::item)
		.def("__setitem__", &Components::set, py::arg("index"), py::arg("value"))
		.def("__delitem__", &Components::erase, py::arg("index"))
		.def("insert", &Components::insert, py::arg("index"), py::arg("value"))
		.def("append", [](const Components& c, py::handle value) { c.insert(c.size(), value); }, py::arg("value"))
		.def("clear", [](const Components& c) { c.resize(0); })
		.def("resize", &Components::resize, py::arg("size"),
			"Rack's vector resize: new component elements are default values, new metadata slots are None.")
		.def("__iter__", [](const Components& c) {
			int n = c.size();
			py::tuple items(n);
			for (int i = 0; i < n; i++)
				items[i] = c.item(i);
			return items.attr("__iter__")();
		});

	auto cls = py::class_<ModuleRef>(engine, "Module", py::custom_type_setup([](PyHeapTypeObject* heap) {
		auto* type = &heap->ht_type;
		type->tp_flags |= Py_TPFLAGS_HAVE_GC;
		type->tp_traverse = traverseModule;
		type->tp_clear = clearModule;
	}), "An owned native Rack module, built with Module.create(configure). Not yet registerable as a plugin model.");
	bindExpanders(engine, cls);  // RP-23A (expander.cpp)
	py::class_<rack::engine::Module::BypassRoute>(cls, "BypassRoute", "A route value; module.bypass_routes returns copies.")
		.def(py::init<>())
		.def_readwrite("input_id", &rack::engine::Module::BypassRoute::inputId)
		.def_readwrite("output_id", &rack::engine::Module::BypassRoute::outputId);

	// --- Lifecycle events (RP-14C3) --------------------------------------------
	bindEvent<M::AddEvent>(cls, "AddEvent", "Rack added the module to its engine.");
	bindEvent<M::RemoveEvent>(cls, "RemoveEvent", "Rack is removing the module from its engine.");
	bindEvent<M::BypassEvent>(cls, "BypassEvent", "Rack bypassed the module.");
	bindEvent<M::UnBypassEvent>(cls, "UnBypassEvent", "Rack enabled the module again.");
	bindEvent<M::ResetEvent>(cls, "ResetEvent", "The user initialized the module.");
	bindEvent<M::RandomizeEvent>(cls, "RandomizeEvent", "The user randomized the module.");
	bindEvent<M::SaveEvent>(cls, "SaveEvent", "The user is saving the patch.");
	bindEvent<M::SetMasterEvent>(cls, "SetMasterEvent", "The module became the engine's master.");
	bindEvent<M::UnsetMasterEvent>(cls, "UnsetMasterEvent", "The module stopped being the engine's master.");
	py::class_<M::PortChangeEvent>(cls, "PortChangeEvent", "A cable connected to or disconnected from a port.")
		.def(py::init([](bool connecting, rack::engine::Port::Type type, int portId) {
			return M::PortChangeEvent{connecting, type, portId};
		}), py::arg("connecting") = false, py::arg("type") = rack::engine::Port::INPUT, py::arg("port_id") = 0)
		.def_readwrite("connecting", &M::PortChangeEvent::connecting)
		.def_readwrite("type", &M::PortChangeEvent::type)
		.def_readwrite("port_id", &M::PortChangeEvent::portId)
		.def("__repr__", [](const M::PortChangeEvent& e) {
			return std::string("Module.PortChangeEvent(connecting=") + (e.connecting ? "True" : "False") + ", type=" +
				(e.type == rack::engine::Port::INPUT ? "INPUT" : "OUTPUT") + ", port_id=" + std::to_string(e.portId) + ")";
		});
	py::class_<M::SampleRateChangeEvent>(cls, "SampleRateChangeEvent", "The engine's sample rate changed, or the module was added.")
		.def(py::init([](float rate, float time) { return M::SampleRateChangeEvent{rate, time}; }),
			py::arg("sample_rate") = 44100.f, py::arg("sample_time") = 1.f / 44100.f)
		.def_readwrite("sample_rate", &M::SampleRateChangeEvent::sampleRate)
		.def_readwrite("sample_time", &M::SampleRateChangeEvent::sampleTime)
		.def("__repr__", [](const M::SampleRateChangeEvent& e) {
			return "Module.SampleRateChangeEvent(sample_rate=" + values::floatRepr(e.sampleRate) + ", sample_time=" +
				values::floatRepr(e.sampleTime) + ")";
		});

	py::class_<M::ExpanderChangeEvent>(cls, "ExpanderChangeEvent", "The module next to this one changed: side 0 is left, 1 is right.")
		.def(py::init([](int side) {
			if (side != 0 && side != 1)
				throw std::invalid_argument("side is 0 (left) or 1 (right)");
			M::ExpanderChangeEvent e;
			e.side = (uint8_t) side;
			return e;
		}), py::arg("side") = 0)
		.def_property("side", [](const M::ExpanderChangeEvent& e) { return (int) e.side; }, [](M::ExpanderChangeEvent& e, int side) {
			if (side != 0 && side != 1)
				throw std::invalid_argument("side is 0 (left) or 1 (right)");
			e.side = (uint8_t) side;
		}, "0 for the left expander, 1 for the right.")
		.def("__repr__", [](const M::ExpanderChangeEvent& e) {
			return "Module.ExpanderChangeEvent(side=" + std::to_string((int) e.side) + ")";
		});
	py::class_<M::ProcessArgs>(cls, "ProcessArgs", "Rack's arguments to process() and processBypass(): a value.")
		.def(py::init([](float rate, float time, int64_t frame) { return M::ProcessArgs{rate, time, frame}; }),
			py::arg("sample_rate") = 44100.f, py::arg("sample_time") = 1.f / 44100.f, py::arg("frame") = 0)
		.def_readwrite("sample_rate", &M::ProcessArgs::sampleRate, "The sample rate in Hz.")
		.def_readwrite("sample_time", &M::ProcessArgs::sampleTime, "The time step of process() in seconds: 1 / sample_rate.")
		.def_readwrite("frame", &M::ProcessArgs::frame, "Audio frames since the engine's (or bench's) first.")
		.def("__repr__", [](const M::ProcessArgs& a) {
			return "Module.ProcessArgs(sample_rate=" + values::floatRepr(a.sampleRate) + ", sample_time=" +
				values::floatRepr(a.sampleTime) + ", frame=" + std::to_string(a.frame) + ")";
		});

	cls.def("lifecycle_events", [](const ModuleRef& r) {
		module::Module& m = r.get();
		std::array<module::LifecycleRecord, module::LifecycleRing::CAPACITY> records;
		size_t n = m.lifecycle.drain(records.data(), records.size());
		py::list out;
		for (size_t i = 0; i < n; i++)
			out.append(py::make_tuple(records[i].sequence, eventObject(records[i])));
		return out;
	}, "RackPy's observation of Rack's events: (sequence, event) pairs since the last call, oldest first. "
		"Not a Rack API. A full queue drops new events; see dropped_lifecycle_events.")
		.def_property_readonly("dropped_lifecycle_events", [](const ModuleRef& r) { return r.get().lifecycle.dropped(); },
			"How many events the bounded queue dropped since the module was created.");

	cls.def("on_add", [](const ModuleRef& r, const M::AddEvent& e) { dispatch(r, [&](module::Module& m) { m.onAdd(e); }); }, py::arg("e"))
		.def("on_remove", [](const ModuleRef& r, const M::RemoveEvent& e) { dispatch(r, [&](module::Module& m) { m.onRemove(e); }); }, py::arg("e"))
		.def("on_bypass", [](const ModuleRef& r, const M::BypassEvent& e) { dispatch(r, [&](module::Module& m) { m.onBypass(e); }); }, py::arg("e"))
		.def("on_un_bypass", [](const ModuleRef& r, const M::UnBypassEvent& e) { dispatch(r, [&](module::Module& m) { m.onUnBypass(e); }); }, py::arg("e"))
		.def("on_port_change", [](const ModuleRef& r, const M::PortChangeEvent& e) { dispatch(r, [&](module::Module& m) { m.onPortChange(e); }); }, py::arg("e"))
		.def("on_reset", [](const ModuleRef& r, const M::ResetEvent& e) { dispatch(r, [&](module::Module& m) { m.onReset(e); }); }, py::arg("e"))
		.def("on_randomize", [](const ModuleRef& r, const M::RandomizeEvent& e) { dispatch(r, [&](module::Module& m) { m.onRandomize(e); }); }, py::arg("e"))
		.def("on_sample_rate_change", [](const ModuleRef& r, const M::SampleRateChangeEvent& e) {
			dispatch(r, [&](module::Module& m) { m.onSampleRateChange(e); });
		}, py::arg("e"))
		.def("on_save", [](const ModuleRef& r, const M::SaveEvent& e) { dispatch(r, [&](module::Module& m) { m.onSave(e); }); }, py::arg("e"))
		.def("on_set_master", [](const ModuleRef& r, const M::SetMasterEvent& e) { dispatch(r, [&](module::Module& m) { m.onSetMaster(e); }); }, py::arg("e"))
		.def("on_unset_master", [](const ModuleRef& r, const M::UnsetMasterEvent& e) { dispatch(r, [&](module::Module& m) { m.onUnsetMaster(e); }); }, py::arg("e"));
	// Rack's deprecated no-argument events: empty in Rack, called by the default handlers.
	for (const char* name : {"on_add", "on_remove", "on_reset", "on_randomize", "on_sample_rate_change"}) {
		std::string pyName = name;
		cls.def(name, [pyName](const ModuleRef& r) {
			deprecated(("Module." + pyName + "() without an event is deprecated in Rack; pass the event").c_str());
			dispatch(r, [&](module::Module& m) {
				if (pyName == "on_add") m.onAdd();
				else if (pyName == "on_remove") m.onRemove();
				else if (pyName == "on_reset") m.onReset();
				else if (pyName == "on_randomize") m.onRandomize();
				else m.onSampleRateChange();
			});
		});
	}

	// --- JSON (RP-15A) ---------------------------------------------------------
	// Native kernels (RP-17A): the module's production audio path.
	cls.def("set_kernel", [](const ModuleRef& r, py::handle kernel) {
		if (r.get().laboratory)
			throw runtime::Laboratory("a laboratory module's audio is its Python process(); give an ordinary module the kernel");
		attachKernel(r, kernel);
	}, py::arg("kernel"),
		"Make `kernel` this module's audio path: Rack's engine calls it every frame, with no Python. Its wiring is "
		"checked against the module's configuration. In the constructor callback, or later to replace the "
		"kernel at the next frame boundary. Main thread.")
		.def_property_readonly("kernel", &moduleKernel, "The kernel last set, or None.")
		.def_property_readonly("kernel_failed", [](const ModuleRef& r) {
			runtime::requireMainThread("reading a module's kernel");
			return r.get().kernels.failed();
		}, "True after the kernel reported a failure: the module's outputs are silent until set_kernel or a reset.");
	cls.def("set_event_handlers", [](const ModuleRef& r, py::kwargs handlers) {
		r.state->requireConstruction();
		using K = module::LifecycleKind;
		static const std::pair<const char*, K> names[] = {{"on_add", K::Add}, {"on_remove", K::Remove},
			{"on_bypass", K::Bypass}, {"on_un_bypass", K::UnBypass}, {"on_port_change", K::PortChange},
			{"on_reset", K::Reset}, {"on_randomize", K::Randomize}, {"on_save", K::Save}};
		auto installed = std::make_unique<PyEventHandlers>();
		bool any = false;
		for (auto item : handlers) {
			std::string key = item.first.cast<std::string>();
			const auto* found = std::find_if(std::begin(names), std::end(names), [&](const auto& n) { return key == n.first; });
			if (found == std::end(names))
				throw py::type_error("set_event_handlers() got an unexpected event " + key +
					" (on_add, on_remove, on_bypass, on_un_bypass, on_port_change, on_reset, on_randomize, on_save; "
					"sample-rate and master events can run on Rack's audio threads, where Python never runs: "
					"rackpy.laboratory runs such overrides offline)");
			if (item.second.is_none())
				continue;
			if (!PyCallable_Check(item.second.ptr()))
				throw py::type_error(key + " must be callable or None");
			installed->handlers[(size_t) found->second] = runtime::PyRef(py::reinterpret_borrow<py::object>(item.second));
			any = true;
		}
		r.get().eventHandlers = any ? std::move(installed) : nullptr;
	}, "Python handlers for Rack's module events, called with the event after Rack's own behaviour, on the "
		"main thread, errors contained: on_add, on_remove, on_bypass, on_un_bypass, on_port_change, on_reset, "
		"on_randomize (Rack's audio stops while these run: keep them under 1 ms) and on_save. Constructor-only; "
		"replaces earlier handlers.");
	cls.def_property_readonly("model", [](const ModuleRef& r) { return modelObject(r.get().model); },
		"Rack's Module::model: the module's rackpy.raw.plugin.Model, or None before Rack creates it from one.")
		.def("get_model", [](const ModuleRef& r) { return modelObject(r.get().getModel()); });
	cls.def("set_data_handlers", [](const ModuleRef& r, py::object toJson, py::object fromJson) {
		r.state->requireConstruction();
		for (const py::object& f : {toJson, fromJson})
			if (!f.is_none() && !PyCallable_Check(f.ptr()))
				throw py::type_error("data handlers must be callable or None");
		std::unique_ptr<PyDataHandlers> handlers;
		if (!toJson.is_none() || !fromJson.is_none()) {
			handlers = std::make_unique<PyDataHandlers>();
			if (!toJson.is_none())
				handlers->toJsonFn = runtime::PyRef(toJson);
			if (!fromJson.is_none())
				handlers->fromJsonFn = runtime::PyRef(fromJson);
		}
		r.get().dataHandlers = std::move(handlers);
	}, py::arg("to_json") = py::none(), py::arg("from_json") = py::none(),
		"Rack's dataToJson/dataFromJson for this module: to_json() returns JSON-compatible data or None (no data); "
		"from_json(data) receives it. Constructor-only.")
		.def("to_json", [](const ModuleRef& r) {
			module::Module& m = sealedModule(r);
			if (!m.model && !m.laboratory)  // a laboratory module's toJson raises its own error
				throw runtime::Ownership("Rack's Module::toJson names the module's model, and this module has none yet (RP-16)");
			json_t* json = moduleJson(r, "Module.to_json", jsonNeedsRack(m), [](module::Module& m) { return m.toJson(); },
				[](module::Module& m) { return APP->engine->moduleToJson(&m); });
			JsonPtr owner(json, json_decref);
			return runtime::fromJson(json);
		}, "Rack's Module::toJson: id, plugin, model, version, params, bypass, expanders and data.")
		.def("from_json", [](const ModuleRef& r, py::handle data) {
			module::Module& m = sealedModule(r);
			if (!m.model && !m.laboratory)
				throw runtime::Ownership("Rack's Module::fromJson checks the module's model, and this module has none yet (RP-16)");
			JsonPtr json(runtime::toJson(data), json_decref);
			moduleJson(r, "Module.from_json", jsonNeedsRack(m), [&](module::Module& m) { m.fromJson(json.get()); return 0; },
				[&](module::Module& m) { APP->engine->moduleFromJson(&m, json.get()); return 0; });
		}, py::arg("data"))
		.def("params_to_json", [](const ModuleRef& r) {
			auto native = [](module::Module& m) { return m.paramsToJson(); };
			json_t* json = moduleJson(r, "Module.params_to_json", true, native, native);
			JsonPtr owner(json, json_decref);
			return runtime::fromJson(json);
		})
		.def("params_from_json", [](const ModuleRef& r, py::handle data) {
			JsonPtr json(runtime::toJson(data), json_decref);
			auto native = [&](module::Module& m) { m.paramsFromJson(json.get()); return 0; };
			moduleJson(r, "Module.params_from_json", true, native, native);
		}, py::arg("data"))
		.def("data_to_json", [](const ModuleRef& r) {
			auto native = [](module::Module& m) { return m.dataToJson(); };
			json_t* json = moduleJson(r, "Module.data_to_json", false, native, native);
			JsonPtr owner(json, json_decref);
			return runtime::fromJson(json);
		}, "The module's custom data, as Rack saves it: None for none.")
		.def("data_from_json", [](const ModuleRef& r, py::handle data) {
			JsonPtr json(runtime::toJson(data), json_decref);
			auto native = [&](module::Module& m) { m.dataFromJson(json.get()); return 0; };
			moduleJson(r, "Module.data_from_json", false, native, native);
		}, py::arg("data"));

	// --- Patch storage (RP-15B) -------------------------------------------------
	auto storage = [](const ModuleRef& r, bool create) {
		module::Module& m = sealedModule(r);
		liveContext("patch storage");
		// Rack raises its own Exception for a module without an engine ID.
		py::gil_scoped_release release;  // createDirectories is blocking filesystem work
		return create ? m.createPatchStorageDirectory() : m.getPatchStorageDirectory();
	};
	cls.def("get_patch_storage_directory", [storage](const ModuleRef& r) { return storage(r, false); },
		"Rack's per-module directory in the patch's autosave folder; needs an engine ID.")
		.def("create_patch_storage_directory", [storage](const ModuleRef& r) { return storage(r, true); },
			"Create (if needed) and return Rack's patch storage directory. Rack, not RackPy, removes it.")
		.def("patch_storage", [storage](const ModuleRef& r, bool create) {
			std::string path = storage(r, create);
			if (create) {
				// Rack's createDirectories ignores failure (RP-15C); the facade does not.
				bool exists;
				{
					py::gil_scoped_release release;
					exists = rack::system::isDirectory(path);
				}
				if (!exists) {
					PyErr_Format(PyExc_OSError, "Rack could not create the patch storage directory %s", path.c_str());
					throw py::error_already_set();
				}
			}
			return py::module_::import("pathlib").attr("Path")(path);
		}, py::arg("create") = true,
			"The patch storage directory as a pathlib.Path, created unless create=False; OSError if it cannot be.");

	cls.def_static("create", &create, py::arg("configure") = py::none())
		.def_static("create", &createConfigured, py::arg("num_params"), py::arg("num_inputs"), py::arg("num_outputs"),
			py::arg("num_lights") = 0, py::arg("configure") = py::none(),
			"Rack's deprecated counting constructor: config() first, then the optional callback. Warns.")
		.def_property("id", [](const ModuleRef& r) { return r.get().id; }, [](const ModuleRef& r, int64_t id) {
			r.state->requireConstruction();
			r.get().id = id;
		}, "Rack's engine ID: -1 until an engine assigns one. Assignable only inside the constructor callback.")
		.def("get_id", [](const ModuleRef& r) { return r.get().getId(); })
		.def("is_bypassed", [](const ModuleRef& r) { return r.get().isBypassed(); },
			"Whether Rack's engine bypasses the module; always False before an engine owns it.")
		.def("close", [](const ModuleRef& r) { r.state->close(); },
			"Delete the owned native module, invalidating all component views. Idempotent.")
		.def_property_readonly("alive", [](const ModuleRef& r) { return r.state->current() != nullptr; })
		.def("config", [](const ModuleRef& r, int p, int i, int o, int l) { r.state->config(p, i, o, l); },
			py::arg("num_params"), py::arg("num_inputs"), py::arg("num_outputs"), py::arg("num_lights") = 0)
		.def("config_param", [](const ModuleRef& r, int i, float min, float max, float def, std::string name,
			std::string unit, float base, float multiplier, float offset, py::object type) {
			if (type.is_none())
				r.state->configQuantity(i, min, max, def, std::move(name), std::move(unit), base, multiplier, offset);
			else
				r.state->configQuantity(i, min, max, def, std::move(name), std::move(unit), base, multiplier, offset, 0, {},
					[&] { return makeTransferredQuantity(r, type, false, i); });
			return component(r, Kind::ParamQuantity, i);
		}, py::arg("param_id"), py::arg("min_value"), py::arg("max_value"), py::arg("default_value"),
			py::arg("name") = "", py::arg("unit") = "", py::arg("display_base") = 0.f,
			py::arg("display_multiplier") = 1.f, py::arg("display_offset") = 0.f, py::arg("quantity_type") = py::none(),
			"Rack's configParam<TParamQuantity>. With quantity_type, construct that ParamQuantity class and transfer "
			"it into the slot; without, configure a built-in quantity and return its checked view.")
		.def("config_switch", [](const ModuleRef& r, int i, float min, float max, float def, std::string name,
			std::vector<std::string> labels, py::object type) {
			if (type.is_none())
				r.state->configQuantity(i, min, max, def, std::move(name), "", 0.f, 1.f, 0.f, 1, std::move(labels));
			else
				r.state->configQuantity(i, min, max, def, std::move(name), "", 0.f, 1.f, 0.f, 1, std::move(labels),
					[&] { return makeTransferredQuantity(r, type, true, i); });
			return component(r, Kind::ParamQuantity, i);
		}, py::arg("param_id"), py::arg("min_value"), py::arg("max_value"), py::arg("default_value"),
			py::arg("name") = "", py::arg("labels") = std::vector<std::string>{}, py::arg("quantity_type") = py::none())
		.def("config_button", [](const ModuleRef& r, int i, std::string name, py::object type) {
			if (type.is_none())
				r.state->configQuantity(i, 0.f, 1.f, 0.f, std::move(name), "", 0.f, 1.f, 0.f, 2);
			else
				r.state->configQuantity(i, 0.f, 1.f, 0.f, std::move(name), "", 0.f, 1.f, 0.f, 2, {},
					[&] { return makeTransferredQuantity(r, type, true, i); });
			return component(r, Kind::ParamQuantity, i);
		}, py::arg("param_id"), py::arg("name") = "", py::arg("quantity_type") = py::none())
		.def("config_input", [](const ModuleRef& r, int i, std::string name, py::object type) {
			return configPort(r, i, std::move(name), std::move(type), true);
		}, py::arg("port_id"), py::arg("name") = "", py::arg("info_type") = py::none(),
			"Rack's configInput<TPortInfo>. With info_type, construct that PortInfo class and transfer it "
			"into the slot; without, configure a built-in PortInfo and return its checked view.")
		.def("config_output", [](const ModuleRef& r, int i, std::string name, py::object type) {
			return configPort(r, i, std::move(name), std::move(type), false);
		}, py::arg("port_id"), py::arg("name") = "", py::arg("info_type") = py::none())
		.def("config_light", [](const ModuleRef& r, int i, std::string name, py::object type) {
			if (type.is_none())
				r.state->configLightInfo(i, std::move(name));
			else
				r.state->configLightInfo(i, std::move(name), [&] {
					return makeTransferred<rack::engine::LightInfo>(r, type, "LightInfo", i, lightSlots);
				});
			return component(r, Kind::LightInfo, i);
		}, py::arg("light_id"), py::arg("name") = "", py::arg("info_type") = py::none())
		.def("config_bypass", [](const ModuleRef& r, int i, int o) { r.state->configBypass(i, o); },
			py::arg("input_id"), py::arg("output_id"))
		.def_property_readonly("bypass_routes", [](const ModuleRef& r) {
			r.get();
			return Components{r.state, Kind::BypassRoute};
		}, "Rack's bypass routes: reading gives copies; inside the constructor callback the collection takes raw edits.");

	struct Group { Kind kind; const char* field; const char* count; const char* get; };
	for (const auto& g : std::vector<Group>{
		{Kind::Param, "params", "get_num_params", "get_param"},
		{Kind::Input, "inputs", "get_num_inputs", "get_input"},
		{Kind::Output, "outputs", "get_num_outputs", "get_output"},
		{Kind::Light, "lights", "get_num_lights", "get_light"},
		{Kind::ParamQuantity, "param_quantities", nullptr, "get_param_quantity"},
		{Kind::InputInfo, "input_infos", nullptr, "get_input_info"},
		{Kind::OutputInfo, "output_infos", nullptr, "get_output_info"},
		{Kind::LightInfo, "light_infos", nullptr, "get_light_info"}
	}) {
		if (g.count)
			cls.def(g.count, [g](const ModuleRef& r) { return count(r, g.kind); });
		cls.def(g.get, [g](const ModuleRef& r, int i) { return component(r, g.kind, i); }, py::arg("index"));
		cls.def_property_readonly(g.field, [g](const ModuleRef& r) {
			r.get();
			return Components{r.state, g.kind};
		});
	}
}

}  // namespace bindings
}  // namespace rackpy
