// SPDX-License-Identifier: GPL-3.0-or-later
// Native construction transaction for RP-14A. No Python objects or virtual
// trampolines live in the module. Engine integration is RP-14C/RP-16.
#pragma once
#include <rack.hpp>

#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>

#include "rackpy/runtime/context.hpp"

namespace rackpy {
namespace module {

/** A Python-owned metadata object keeps this registration alive, not its
module. Expiration removes the subscription; module teardown clears the actual
SDK pointer without touching Python. Registration and module deletion are
main-thread-only. A standalone metadata object's last Python reference may die
on another thread: detach synchronizes with clear before its SDK field dies.
This is not an audio-path primitive; engine-owned metadata factories are separate. */
struct ModuleBackreference {
	explicit ModuleBackreference(rack::engine::Module** target) : target(target) {}
	void clear() {
		std::lock_guard<std::mutex> lock(mutex);
		if (target)
			*target = nullptr;
	}
	void detach() {
		std::lock_guard<std::mutex> lock(mutex);
		target = nullptr;
	}
private:
	std::mutex mutex;
	rack::engine::Module** target;
};

/** Metadata that Python constructed and a module slot now owns (RP-14B2b5).
Retiring hands it back to its Python object instead of deleting it, so a
retained reference raises StaleHandle rather than touching freed memory. The
implementation takes the GIL; slots retire on the thread deleting the module. */
struct TransferredMetadata {
	virtual ~TransferredMetadata() = default;
	virtual bool transferred() const = 0;
	virtual void retire() noexcept = 0;
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

struct Module : rack::engine::Module {
	std::shared_ptr<runtime::ObjectToken> token = std::make_shared<runtime::ObjectToken>(nullptr);
	// One generation per metadata slot. Replacement must invalidate retained
	// views even while the module (and its component token) is still alive.
	using Tokens = std::vector<std::shared_ptr<runtime::ObjectToken>>;
	Tokens paramQuantityTokens, inputInfoTokens, outputInfoTokens, lightInfoTokens;
	std::vector<std::weak_ptr<ModuleBackreference>> backreferences;
	std::shared_ptr<ModuleBackreference> trackBackreference(rack::engine::Module** target) {
		runtime::requireMainThread("linking standalone metadata to a module");
		// Prune on registration, so repeated reassignments/deleted metadata do
		// not leave an unbounded history on a long-lived module.
		backreferences.erase(std::remove_if(backreferences.begin(), backreferences.end(),
			[](const auto& link) { return link.expired(); }), backreferences.end());
		auto link = std::make_shared<ModuleBackreference>(target);
		backreferences.push_back(link);
		return link;
	}
	~Module() override {
		for (auto& weak : backreferences)
			if (auto link = weak.lock())
				link->clear();
		token->invalidate();
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

/** Shared by Python handles, not by component views. The factory alone seals
construction. A failed constructor closes even escaped handles immediately. */
struct Construction {
	std::unique_ptr<Module> object = std::make_unique<Module>();
	bool constructing = true;
	bool configured = false;

	Module& get() const {
		runtime::requireMainThread("using a RackPy module");
		if (!object)
			throw runtime::StaleHandle("this module has been closed or its constructor failed");
		return *object;
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
		index(id, m.paramQuantities.size());
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
		// Param::setValue, cannot throw here. Nothing mutates the Param before
		// preparation has succeeded.
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
	void seal() {
		get();  // also rejects a caught native construction failure
		constructing = false;
	}
	void close() {
		runtime::requireMainThread("closing a RackPy module");
		if (constructing && object)
			throw runtime::Ownership("cannot close a module during its constructor callback");
		object.reset();
	}
	void abort() { object.reset(); }
};

}  // namespace module
}  // namespace rackpy
