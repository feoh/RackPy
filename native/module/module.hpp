// SPDX-License-Identifier: GPL-3.0-or-later
// Native construction transaction for RP-14A. No Python objects or virtual
// trampolines live in the module. Engine integration is RP-14C/RP-16.
#pragma once
#include <rack.hpp>

#include <memory>
#include <stdexcept>

#include "rackpy/runtime/context.hpp"

namespace rackpy {
namespace module {

struct Module : rack::engine::Module {
	std::shared_ptr<runtime::ObjectToken> token = std::make_shared<runtime::ObjectToken>(nullptr);
	// One generation per metadata slot. Replacement must invalidate retained
	// views even while the module (and its component token) is still alive.
	using Tokens = std::vector<std::shared_ptr<runtime::ObjectToken>>;
	Tokens paramQuantityTokens, inputInfoTokens, outputInfoTokens, lightInfoTokens;
	~Module() override {
		token->invalidate();
		for (auto* slots : {&paramQuantityTokens, &inputInfoTokens, &outputInfoTokens, &lightInfoTokens})
			for (auto& slot : *slots)
				slot->invalidate();
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
	These are built-in adapters, not the complete class-factory templates. */
	template <typename Info, typename Prepare, typename Make>
	void replaceInfoWith(int id, std::vector<Info*>& infos, Module::Tokens& tokens, Prepare prepare, Make make) {
		requireConstruction();
		index(id, infos.size());
		auto next = make();
		auto generation = std::make_shared<runtime::ObjectToken>(nullptr);
		prepare(*next);
		tokens[id]->invalidate();
		Info* previous = infos[id];
		infos[id] = next.release();
		tokens[id] = std::move(generation);
		delete previous;
	}
	template <typename Info, typename Prepare>
	void replaceInfo(int id, std::vector<Info*>& infos, Module::Tokens& tokens, Prepare prepare) {
		replaceInfoWith(id, infos, tokens, prepare, [] { return std::make_unique<Info>(); });
	}
	void configPortInfo(int id, std::string name, bool input) {
		requireConstruction();
		Module& m = get();
		replaceInfo(id, input ? m.inputInfos : m.outputInfos, input ? m.inputInfoTokens : m.outputInfoTokens,
			[&](rack::engine::PortInfo& info) {
				info.module = &m;
				info.type = input ? rack::engine::Port::INPUT : rack::engine::Port::OUTPUT;
				info.portId = id;
				info.name = std::move(name);
			});
	}
	void configLightInfo(int id, std::string name) {
		requireConstruction();
		Module& m = get();
		replaceInfo(id, m.lightInfos, m.lightInfoTokens, [&](rack::engine::LightInfo& info) {
			info.module = &m;
			info.lightId = id;
			info.name = std::move(name);
		});
	}
	/** Built-in quantities only: no Python virtuals in the native module.
	Prepare all strings/labels before committing a slot or its Param default. */
	void configQuantity(int id, float min, float max, float def, std::string name, std::string unit,
		float base, float multiplier, float offset, int kind = 0, std::vector<std::string> labels = {}) {
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
			if (kind) {
				q.snapEnabled = true;
				q.smoothEnabled = false;
				q.randomizeEnabled = kind != 2;
				static_cast<rack::engine::SwitchQuantity&>(q).labels = std::move(labels);
			}
		};
		replaceInfoWith(id, m.paramQuantities, m.paramQuantityTokens, prepare,
			[kind]() -> std::unique_ptr<rack::engine::ParamQuantity> {
				if (kind)
					return std::make_unique<rack::engine::SwitchQuantity>();
				return std::make_unique<rack::engine::ParamQuantity>();
			});
		// Built-in getDefaultValue and Param::setValue cannot throw. Nothing
		// mutates the Param before preparation has succeeded.
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
