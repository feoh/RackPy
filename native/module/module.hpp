// SPDX-License-Identifier: GPL-3.0-or-later
// Native construction transaction for RP-14A. No Python objects or virtual
// trampolines live in the module. Engine integration is RP-14C/RP-16.
#pragma once
#include <rack.hpp>

#include <algorithm>
#include <atomic>
#include <memory>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "../engine/expanders.hpp"
#include "../io/endpoints.hpp"
#include "lifecycle.hpp"
#include "../dsp/runtime/runtime.hpp"
#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace module {

/** A Python-owned metadata object keeps this registration alive, not its
module. Expiration removes the subscription; module teardown clears the actual
SDK pointer without touching Python. Registration and module deletion are
main-thread-only. A standalone metadata object's last Python reference may die
on another thread: detach synchronizes with clear before its SDK field dies.
This is not an audio-path primitive; engine-owned metadata factories are separate. */
struct ModuleBackreference {
	explicit ModuleBackreference(rack::engine::Module** target, std::function<void()> cleared = {}, bool external = false)
		: external(external), target(target), cleared(std::move(cleared)) {}
	/** A widget's pointer (a control), not one of the module's own metadata. */
	const bool external;
	void clear() {
		std::lock_guard<std::mutex> lock(mutex);
		if (target) {
			*target = nullptr;
			if (cleared)
				cleared();  // never throws (callers make it noexcept-safe)
		}
	}
	void detach() {
		std::lock_guard<std::mutex> lock(mutex);
		target = nullptr;
	}
	bool attached() {
		std::lock_guard<std::mutex> lock(mutex);
		return target != nullptr;
	}
private:
	std::mutex mutex;
	rack::engine::Module** target;
	std::function<void()> cleared;
};

/** Metadata that Python constructed and a module slot now owns (RP-14B2b5).
Retiring hands it back to its Python object instead of deleting it, so a
retained reference raises StaleHandle rather than touching freed memory. The
implementation takes the GIL; slots retire on the thread deleting the module. */
struct TransferredMetadata {
	virtual ~TransferredMetadata() = default;
	virtual bool transferred() const = 0;
	/** The slot deletes it, as Rack's delete-then-new helpers and ~Module do. */
	virtual void retire() noexcept = 0;
	/** A raw vector edit drops the pointer without deleting the object: it
	becomes an ordinary Python-owned object again (RP-14B2b7). */
	virtual void release() noexcept = 0;
	/** Its Python object (a PyObject*), for the owner's garbage-collector traversal. */
	virtual void* pythonObject() const noexcept = 0;
};

/** The transferred object in a slot, or null for native-created metadata. */
template <typename Info>
TransferredMetadata* transferredIn(Info* info) {
	auto* t = dynamic_cast<TransferredMetadata*>(info);
	return t && t->transferred() ? t : nullptr;
}

/** Release a slot's previous metadata, as Rack's delete-then-new helpers do. */
template <typename Info>
void dispose(Info* info) {
	if (auto* t = transferredIn(info))
		t->retire();
	else
		delete info;
}

struct Construction;

/** Python's custom state for one module (RP-15A): Rack's dataToJson and
dataFromJson call these synchronously. The implementation lives with the
bindings; this interface keeps the module Python-free. */
struct DataHandlers {
	virtual ~DataHandlers() = default;
	/** A new reference, or null for "no data". Never throws. */
	virtual json_t* toJson() noexcept = 0;
	/** `data` is borrowed. Never throws. */
	virtual void fromJson(json_t* data) noexcept = 0;
};

/** Python's handlers for the module's lifecycle events (RP-16C). Rack's
adapters call dispatch() after Rack's own behaviour; the implementation
lives with the bindings and contains every error. */
struct EventHandlers {
	virtual ~EventHandlers() = default;
	/** Never throws. Runs Python only on the main thread; refused elsewhere. */
	virtual void dispatch(const LifecycleRecord& record) noexcept = 0;
};

/** A pointer a raw vector edit removes. Native-created metadata has no other
owner, so it is deleted; its views are invalidated by their slot token. */
template <typename Info>
void releaseFromSlot(Info* info) {
	if (auto* t = transferredIn(info))
		t->release();
	else
		delete info;
}

#ifdef RACKPY_TEST_HOOKS
/** Test-hooks builds only (RP-38): the RP-07 audio guard's marks around RackPy's own engine-thread code, when
Rack runs with native/testing/audio_guard.c preloaded. testing::registerModels resolves them once, at plugin
start on the main thread; process() only calls them. */
inline void (*audioGuardEnter)() = nullptr;
inline void (*audioGuardLeave)() = nullptr;
struct AudioGuardMark {
	AudioGuardMark() {
		if (audioGuardEnter)
			audioGuardEnter();
	}
	~AudioGuardMark() {
		if (audioGuardLeave)
			audioGuardLeave();
	}
};
#endif

struct Module : rack::engine::Module, engine::ExpanderChannels, io::Endpoints {
	std::shared_ptr<runtime::ObjectToken> token = std::make_shared<runtime::ObjectToken>(nullptr);
	// A callback-laboratory module (RP-22, native/laboratory): runs only on a
	// laboratory Bench, never in Rack's engine.
	const bool laboratory = false;
	// While a laboratory Bench steps it (main thread): it cannot be closed or go to Rack's engine.
	bool onBench = false;
	Module() = default;
	explicit Module(bool laboratory) : laboratory(laboratory) {}
	// Set once Rack's engine owns the module (RP-14C2). Cleared only when Python
	// takes it back after removing it from the engine (Construction::reclaimFromRack).
	std::atomic<bool> engineOwned{false};
	// The Python handles' shared state, while any handle lives (see handleState).
	std::weak_ptr<Construction> construction;
	// Events Rack dispatched, for Python to read later on the main thread.
	LifecycleRing lifecycle;
	// Set inside the constructor callback (Module.set_data_handlers); may be null.
	std::unique_ptr<DataHandlers> dataHandlers;
	// Set inside the constructor callback (Module.set_event_handlers); may be null.
	std::unique_ptr<EventHandlers> eventHandlers;
	void handle(const LifecycleRecord& record) {
		lifecycle.push(record);
		if (eventHandlers)
			eventHandlers->dispatch(record);
	}
	// The native kernel Python chose (RP-17A): the module's production audio path.
	dsp::KernelSlot kernels;
	// The kernel Rack's engine runs instead while the module is bypassed (RP-34); until one is
	// attached, Rack's own processBypass copies the bypass routes.
	dsp::KernelSlot bypassKernels;
	// RackPy expander message buffers Python put in this module's expander slots (RP-23): kept
	// alive as long as the module, as Rack expects a module to own its buffers. Main thread. The
	// channels they form are published in the engine::ExpanderChannels base, for native code.
	std::vector<std::shared_ptr<engine::MessageBuffer>> expanderMessages;
	// The module's audio port and MIDI inputs (RP-31J), in the io::Endpoints base: a frame of them around the kernel.
	void process(const ProcessArgs& args) override {
#ifdef RACKPY_TEST_HOOKS
		AudioGuardMark mark;
#endif
		if (__builtin_expect(io::Endpoints::active, 0)) {
			beginFrame(args.frame);
			kernels.process(*this, args);
			endFrame();
			return;
		}
		kernels.process(*this, args);
	}
	void processBypass(const ProcessArgs& args) override {
#ifdef RACKPY_TEST_HOOKS
		AudioGuardMark mark;
#endif
		if (!bypassKernels.engaged()) {
			rack::engine::Module::processBypass(args);
			return;
		}
		if (__builtin_expect(io::Endpoints::active, 0)) {
			beginFrame(args.frame);
			bypassKernels.process(*this, args);
			endFrame();
			return;
		}
		bypassKernels.process(*this, args);
	}
	json_t* dataToJson() override;
	void dataFromJson(json_t* rootJ) override;

	// Native lifecycle adapters (lifecycle.cpp, Python-free): Rack's behaviour,
	// then a snapshot. No Module virtual dispatches to Python.
	void onAdd(const AddEvent& e) override;
	void onRemove(const RemoveEvent& e) override;
	void onBypass(const BypassEvent& e) override;
	void onUnBypass(const UnBypassEvent& e) override;
	void onPortChange(const PortChangeEvent& e) override;
	void onReset(const ResetEvent& e) override;
	void onRandomize(const RandomizeEvent& e) override;
	void onSampleRateChange(const SampleRateChangeEvent& e) override;
	void onSave(const SaveEvent& e) override;
	void onSetMaster(const SetMasterEvent& e) override;
	void onUnsetMaster(const UnsetMasterEvent& e) override;
	void onExpanderChange(const ExpanderChangeEvent& e) override;
	// Keep the deprecated no-argument events visible next to the overrides.
	using rack::engine::Module::onAdd;
	using rack::engine::Module::onRemove;
	using rack::engine::Module::onReset;
	using rack::engine::Module::onRandomize;
	using rack::engine::Module::onSampleRateChange;
	// Component views point into a vector's storage, so each component vector
	// has its own generation: a structural edit (RP-14B2b7) invalidates only
	// the views of that vector, like C++ iterator invalidation.
	std::shared_ptr<runtime::ObjectToken> paramsToken = std::make_shared<runtime::ObjectToken>(nullptr);
	std::shared_ptr<runtime::ObjectToken> inputsToken = std::make_shared<runtime::ObjectToken>(nullptr);
	std::shared_ptr<runtime::ObjectToken> outputsToken = std::make_shared<runtime::ObjectToken>(nullptr);
	std::shared_ptr<runtime::ObjectToken> lightsToken = std::make_shared<runtime::ObjectToken>(nullptr);
	// Bypass routes are copied out, never borrowed; edits renew this for uniformity.
	std::shared_ptr<runtime::ObjectToken> routesToken = std::make_shared<runtime::ObjectToken>(nullptr);
	// One generation per metadata slot. Replacement must invalidate retained
	// views even while the module (and its component token) is still alive.
	using Tokens = std::vector<std::shared_ptr<runtime::ObjectToken>>;
	Tokens paramQuantityTokens, inputInfoTokens, outputInfoTokens, lightInfoTokens;
	std::vector<std::weak_ptr<ModuleBackreference>> backreferences;
	/** Whether a widget (a control) still points at this module. */
	bool referenced() const {
		for (const auto& weak : backreferences)
			if (auto link = weak.lock(); link && link->external && link->attached())
				return true;
		return false;
	}
	std::shared_ptr<ModuleBackreference> trackBackreference(rack::engine::Module** target, std::function<void()> cleared = {},
		bool external = false) {
		runtime::requireMainThread("linking standalone metadata to a module");
		// Prune on registration, so repeated reassignments/deleted metadata do
		// not leave an unbounded history on a long-lived module.
		backreferences.erase(std::remove_if(backreferences.begin(), backreferences.end(),
			[](const auto& link) { return link.expired(); }), backreferences.end());
		auto link = std::make_shared<ModuleBackreference>(target, std::move(cleared), external);
		backreferences.push_back(link);
		return link;
	}
	~Module() override {
		// First, while the module is whole: its audio port no longer reaches it, and it is not Rack's master.
		io::Endpoints::detach();
		for (auto& weak : backreferences)
			if (auto link = weak.lock())
				link->clear();
		token->invalidate();
		for (auto* vector : {&paramsToken, &inputsToken, &outputsToken, &lightsToken})
			(*vector)->invalidate();
		for (auto* slots : {&paramQuantityTokens, &inputInfoTokens, &outputInfoTokens, &lightInfoTokens})
			for (auto& slot : *slots)
				slot->invalidate();
		// Rack's base destructor deletes every slot. Take transferred metadata
		// out first; retiring may run Python finalizers, so empty the slot before.
		retireTransferred(paramQuantities);
		retireTransferred(inputInfos);
		retireTransferred(outputInfos);
		retireTransferred(lightInfos);
	}
	template <typename Info>
	static void retireTransferred(std::vector<Info*>& infos) {
		for (auto& info : infos) {
			if (auto* t = transferredIn(info)) {
				info = nullptr;
				t->retire();
			}
		}
	}
	static Tokens tokens(size_t size) {
		Tokens result;
		result.reserve(size);
		while (result.size() < size)
			result.push_back(std::make_shared<runtime::ObjectToken>(nullptr));
		return result;
	}
};

/** Whether a quantity of this module may be a smoothing target. Rack's Engine
keeps a raw pointer to its target and clears it only in removeModule
(Engine.cpp:815), so only a module Rack's engine owns qualifies; Python's calls
refuse smoothing otherwise. */
inline bool smoothingAllowed(const rack::engine::Module* module) {
	auto* m = dynamic_cast<const Module*>(module);
	return m && m->engineOwned.load(std::memory_order_acquire);
}

/** Shared by Python handles, not by component views. The factory alone seals
construction. A failed constructor closes even escaped handles immediately.
After releaseToRack() Rack's engine owns the module: the handles borrow it
through its token, and Rack deletes it (RP-14C2). */
struct Construction {
	std::unique_ptr<Module> object = std::make_unique<Module>();
	Module* rackOwned = nullptr;
	std::shared_ptr<runtime::ObjectToken> rackToken;
	bool constructing = true;
	bool configured = false;

	Construction() = default;
	/** A construction of a Module subclass (a laboratory module). */
	explicit Construction(std::unique_ptr<Module> object) : object(std::move(object)) {}
	/** Handles for a module Rack's engine already owns, after its earlier
	handles were all dropped. Main thread. */
	static std::shared_ptr<Construction> handleState(Module& m) {
		runtime::requireMainThread("finding a RackPy module");
		if (auto existing = m.construction.lock())
			return existing;
		if (!m.engineOwned.load(std::memory_order_acquire) || !m.token->alive())
			throw runtime::Ownership("only a module Rack's engine owns can be found again");
		auto state = std::make_shared<Construction>();
		state->object.reset();
		state->rackOwned = &m;
		state->rackToken = m.token;
		state->constructing = false;
		state->configured = true;
		m.construction = state;
		return state;
	}

	/** The live module, whoever owns it, or null. Main thread for anything but a liveness test. */
	Module* current() const {
		if (object)
			return object.get();
		if (rackOwned && rackToken->alive())
			return rackOwned;
		return nullptr;
	}
	Module& get() const {
		runtime::requireMainThread("using a RackPy module");
		if (Module* m = current())
			return *m;
		if (rackOwned)
			throw runtime::StaleHandle("Rack has deleted this module");
		throw runtime::StaleHandle("this module has been closed or its constructor failed");
	}
	/** Hand a sealed module to Rack's engine, which deletes it after
	Engine::removeModule. The caller adds it to the engine next. */
	Module* releaseToRack() {
		Module& m = get();
		if (constructing)
			throw runtime::Ownership("a module can go to Rack's engine only after its constructor callback returns");
		if (!object)
			throw runtime::Ownership("Rack's engine already owns this module");
		if (m.laboratory)
			throw runtime::Laboratory("a laboratory module runs only on a laboratory Bench, never in Rack's engine");
		if (m.onBench)
			throw runtime::Ownership("remove the module from its laboratory Bench first");
		rackToken = m.token;
		rackOwned = object.release();
		rackOwned->engineOwned.store(true, std::memory_order_release);
		return rackOwned;
	}
	/** Python takes back a module Rack's engine no longer has (Engine.remove_module, RP-24A):
	the caller has removed it from the engine, and nothing of Rack's still points at it. */
	void reclaimFromRack() {
		runtime::requireMainThread("taking a module back from Rack");
		if (object || !rackOwned || !rackToken->alive())
			throw runtime::Ownership("only a module Rack's engine owned can be taken back");
		object.reset(rackOwned);
		rackOwned = nullptr;
		rackToken.reset();
		object->engineOwned.store(false, std::memory_order_release);
	}
	void requireConstruction() const {
		get();
		if (!constructing)
			throw runtime::Ownership("module configuration is allowed only inside Module.create's constructor callback");
	}
	void config(int params, int inputs, int outputs, int lights) {
		requireConstruction();
		if (configured)
			throw runtime::Ownership("Module.config may be called only once, including for zero counts");
		if (params < 0 || inputs < 0 || outputs < 0 || lights < 0)
			throw std::invalid_argument("component counts must be nonnegative");
		configured = true;
		try {
			object->config(params, inputs, outputs, lights);
			object->paramQuantityTokens = Module::tokens(params);
			object->inputInfoTokens = Module::tokens(inputs);
			object->outputInfoTokens = Module::tokens(outputs);
			object->lightInfoTokens = Module::tokens(lights);
		}
		catch (...) {
			// Rack can have partially resized its vectors. Never reuse that object.
			object.reset();
			throw;
		}
	}
	static int index(int i, size_t size) {
		if (i < 0 || static_cast<size_t>(i) >= size)
			throw std::out_of_range("module component ID out of range");
		return i;
	}
	/** Unlike Rack's delete-then-new helpers, prepare before retiring the old
	metadata. Failed allocation/string preparation leaves the old slot valid.
	`make` may run Python (a class factory) that reenters this construction, so
	it must recheck the phase and ID before it transfers anything; nothing
	after it throws. */
	template <typename Info, typename Prepare, typename Make>
	void replaceInfoWith(int id, std::vector<Info*>& infos, Module::Tokens& tokens, Prepare prepare, Make make) {
		requireConstruction();
		index(id, infos.size());
		auto generation = std::make_shared<runtime::ObjectToken>(nullptr);
		auto next = make();
		prepare(*next);
		tokens[id]->invalidate();
		Info* previous = infos[id];
		infos[id] = next.release();
		tokens[id] = std::move(generation);
		dispose(previous);
	}
	template <typename Info, typename Prepare>
	void replaceInfo(int id, std::vector<Info*>& infos, Module::Tokens& tokens, Prepare prepare) {
		replaceInfoWith(id, infos, tokens, prepare, [] { return std::make_unique<Info>(); });
	}
	/** Rack's configInput/configOutput<TPortInfo>: `make` returns the new
	TPortInfo, then the same fields as Rack's template are set on it. */
	template <typename Make>
	void configPortInfo(int id, std::string name, bool input, Make make) {
		requireConstruction();
		Module& m = get();
		replaceInfoWith(id, input ? m.inputInfos : m.outputInfos, input ? m.inputInfoTokens : m.outputInfoTokens,
			[&](rack::engine::PortInfo& info) {
				info.module = &m;
				info.type = input ? rack::engine::Port::INPUT : rack::engine::Port::OUTPUT;
				info.portId = id;
				info.name = std::move(name);
			}, make);
	}
	void configPortInfo(int id, std::string name, bool input) {
		configPortInfo(id, std::move(name), input, [] { return std::make_unique<rack::engine::PortInfo>(); });
	}
	template <typename Make>
	void configLightInfo(int id, std::string name, Make make) {
		requireConstruction();
		Module& m = get();
		replaceInfoWith(id, m.lightInfos, m.lightInfoTokens, [&](rack::engine::LightInfo& info) {
			info.module = &m;
			info.lightId = id;
			info.name = std::move(name);
		}, make);
	}
	void configLightInfo(int id, std::string name) {
		configLightInfo(id, std::move(name), [] { return std::make_unique<rack::engine::LightInfo>(); });
	}
	/** Rack's configParam/configSwitch/configButton<TQuantity>. `make` returns
	the new quantity (kind 0: any ParamQuantity, otherwise a SwitchQuantity).
	Prepare all strings/labels before committing a slot or its Param default. */
	void configQuantity(int id, float min, float max, float def, std::string name, std::string unit,
		float base, float multiplier, float offset, int kind = 0, std::vector<std::string> labels = {}) {
		configQuantity(id, min, max, def, std::move(name), std::move(unit), base, multiplier, offset, kind,
			std::move(labels), [kind]() -> std::unique_ptr<rack::engine::ParamQuantity> {
				if (kind)
					return std::make_unique<rack::engine::SwitchQuantity>();
				return std::make_unique<rack::engine::ParamQuantity>();
			});
	}
	template <typename Make>
	void configQuantity(int id, float min, float max, float def, std::string name, std::string unit,
		float base, float multiplier, float offset, int kind, std::vector<std::string> labels, Make make) {
		requireConstruction();
		Module& m = get();
		index(id, std::min(m.params.size(), m.paramQuantities.size()));
		if (!std::isfinite(min) || !std::isfinite(max) || !(min < max) || !std::isfinite(def) || def < min || def > max)
			throw std::invalid_argument("quantity bounds must be finite and increasing, with default inside the range");
		if (!std::isfinite(base) || !std::isfinite(multiplier) || !std::isfinite(offset) || base == 1.f || base == -1.f)
			throw std::invalid_argument("display coefficients must be finite; base cannot be 1 or -1");
		auto prepare = [&](rack::engine::ParamQuantity& q) {
			q.module = &m;
			q.paramId = id;
			q.minValue = min;
			q.maxValue = max;
			q.defaultValue = def;
			q.name = std::move(name);
			q.unit = std::move(unit);
			q.displayBase = base;
			q.displayMultiplier = multiplier;
			q.displayOffset = offset;
			// As Rack's configSwitch/configButton: only these fields, so a
			// class factory's constructor keeps everything else it set.
			if (kind) {
				q.snapEnabled = true;
				q.smoothEnabled = false;
			}
			if (kind == 1)
				static_cast<rack::engine::SwitchQuantity&>(q).labels = std::move(labels);
			if (kind == 2)
				q.randomizeEnabled = false;
		};
		replaceInfoWith(id, m.paramQuantities, m.paramQuantityTokens, prepare, make);
		// getDefaultValue is native (Python cannot override it) and, like
		// Param::setValue, cannot throw here, except on a laboratory quantity
		// (RP-22C), whose Python error leaves the committed slot and an unset
		// Param default. Nothing mutates the Param before preparation has succeeded.
		m.params[id].setValue(m.paramQuantities[id]->getDefaultValue());
	}
	void configBypass(int input, int output) {
		requireConstruction();
		Module& m = get();
		index(input, m.inputs.size());
		index(output, m.outputs.size());
		for (const auto& route : m.bypassRoutes) {
			if (route.outputId == output)
				throw std::invalid_argument("this output already has a bypass route");
		}
		m.configBypass(input, output);
	}
	// --- Raw vector edits (RP-14B2b7): constructor-only, after config. --------
	// Callers prepare everything that can throw (generations, capacity, the
	// Python object's transfer) before calling these, which then cannot throw.

	void requireRawEdit() const {
		requireConstruction();
		if (!configured)
			throw runtime::Ownership("call Module.config before editing the module's vectors");
	}
	/** Validate an index for an edit, Python-style: negative counts from the end. */
	static size_t editIndex(long i, size_t size) {
		if (i < 0)
			i += static_cast<long>(size);
		if (i < 0 || static_cast<size_t>(i) >= size)
			throw std::out_of_range("module vector index out of range");
		return static_cast<size_t>(i);
	}
	/** An insertion position, clamped like list.insert. */
	static size_t insertIndex(long i, size_t size) {
		if (i < 0)
			i += static_cast<long>(size);
		return static_cast<size_t>(std::max(0L, std::min(i, static_cast<long>(size))));
	}
	template <typename Info>
	static void setSlot(std::vector<Info*>& infos, Module::Tokens& tokens, size_t i, Info* next,
		std::shared_ptr<runtime::ObjectToken> generation) noexcept {
		tokens[i]->invalidate();
		Info* previous = infos[i];
		infos[i] = next;
		tokens[i] = std::move(generation);
		releaseFromSlot(previous);
	}
	/** Capacity first: with it, insert() cannot reallocate or throw. */
	template <typename Info>
	static void reserveSlots(std::vector<Info*>& infos, Module::Tokens& tokens, size_t size) {
		infos.reserve(size);
		tokens.reserve(size);
	}
	template <typename Info>
	static void insertSlot(std::vector<Info*>& infos, Module::Tokens& tokens, size_t i, Info* next,
		std::shared_ptr<runtime::ObjectToken> generation) noexcept {
		infos.insert(infos.begin() + i, next);
		tokens.insert(tokens.begin() + i, std::move(generation));
	}
	template <typename Info>
	static void eraseSlot(std::vector<Info*>& infos, Module::Tokens& tokens, size_t i) noexcept {
		tokens[i]->invalidate();
		Info* previous = infos[i];
		infos.erase(infos.begin() + i);
		tokens.erase(tokens.begin() + i);
		releaseFromSlot(previous);
	}
	/** Shrink releases the tail; growth appends null slots, with `added` generations. */
	template <typename Info>
	static void resizeSlots(std::vector<Info*>& infos, Module::Tokens& tokens, size_t size, Module::Tokens added) noexcept {
		while (infos.size() > size)
			eraseSlot(infos, tokens, infos.size() - 1);
		for (auto& generation : added) {
			infos.push_back(nullptr);
			tokens.push_back(std::move(generation));
		}
	}
	/** After a structural edit of a component vector, its old views die. */
	static void renew(std::shared_ptr<runtime::ObjectToken>& vectorToken, std::shared_ptr<runtime::ObjectToken> next) noexcept {
		vectorToken->invalidate();
		vectorToken = std::move(next);
	}

	/** Rack's code relies on these invariants without checking them: matching
	component/metadata sizes, a quantity for every parameter and metadata for
	every port (dereferenced by save, reset and tooltips), each object's module
	and ID matching its slot, and bypass routes inside the port vectors. */
	[[noreturn]] static void fail(const std::string& what) {
		throw std::invalid_argument("module construction left " + what);
	}
	void validate() const {
		Module& m = *object;
		if (m.params.size() != m.paramQuantities.size())
			fail(std::to_string(m.params.size()) + " params but " + std::to_string(m.paramQuantities.size()) + " parameter quantities");
		if (m.inputs.size() != m.inputInfos.size() || m.outputs.size() != m.outputInfos.size())
			fail("a port vector and its metadata vector with different sizes");
		if (m.lights.size() != m.lightInfos.size())
			fail(std::to_string(m.lights.size()) + " lights but " + std::to_string(m.lightInfos.size()) + " light metadata slots");
		for (size_t i = 0; i < m.paramQuantities.size(); i++) {
			auto* q = m.paramQuantities[i];
			if (!q)
				fail("parameter " + std::to_string(i) + " without a quantity");
			if (q->module != &m || q->paramId != static_cast<int>(i))
				fail("parameter quantity " + std::to_string(i) + " not set to this module and param_id " + std::to_string(i));
		}
		for (auto* infos : {&m.inputInfos, &m.outputInfos}) {
			auto type = infos == &m.inputInfos ? rack::engine::Port::INPUT : rack::engine::Port::OUTPUT;
			for (size_t i = 0; i < infos->size(); i++) {
				auto* info = (*infos)[i];
				if (!info)
					fail(std::string(type == rack::engine::Port::INPUT ? "input " : "output ") + std::to_string(i) + " without metadata");
				if (info->module != &m || info->portId != static_cast<int>(i) || info->type != type)
					fail("port metadata " + std::to_string(i) + " not set to this module, its port_id and its type");
			}
		}
		for (size_t i = 0; i < m.lightInfos.size(); i++) {
			auto* info = m.lightInfos[i];
			if (info && (info->module != &m || info->lightId != static_cast<int>(i)))
				fail("light metadata " + std::to_string(i) + " not set to this module and light_id " + std::to_string(i));
		}
		for (const auto& route : m.bypassRoutes) {
			if (route.inputId < 0 || static_cast<size_t>(route.inputId) >= m.inputs.size() || route.outputId < 0 ||
				static_cast<size_t>(route.outputId) >= m.outputs.size())
				fail("a bypass route outside the port vectors");
		}
	}
	void seal() {
		get();  // also rejects a caught native construction failure
		validate();
		constructing = false;
	}
	void close() {
		runtime::requireMainThread("closing a RackPy module");
		if (constructing && object)
			throw runtime::Ownership("cannot close a module during its constructor callback");
		if (current() && !object)
			throw runtime::Ownership("Rack's engine owns this module, and Rack deletes it");
		if (object && object->onBench)
			throw runtime::Ownership("remove the module from its laboratory Bench before closing it");
		object.reset();
	}
	void abort() { object.reset(); }
};

}  // namespace module
}  // namespace rackpy
