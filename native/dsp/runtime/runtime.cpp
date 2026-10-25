// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "rackpy/runtime/diagnostics.hpp"

namespace rackpy {
namespace dsp {

KernelSlot::~KernelSlot() {
	// Rack removes a module from its engine before deleting it, so nothing runs here concurrently.
	reclaim();
	delete pending.exchange(nullptr);
	delete current;
}

void KernelSlot::silence(rack::engine::Module& module) noexcept {
	for (auto& output : module.outputs)
		std::fill(std::begin(output.voltages), std::end(output.voltages), 0.f);
}

void KernelSlot::process(rack::engine::Module& module, const rack::engine::Module::ProcessArgs& args) noexcept {
	// Pick up a kernel the main thread handed over, if the old one can be retired.
	if (pending.load(std::memory_order_acquire) && (!current || retired.size() < RETIRED_CAPACITY)) {
		if (Hosted* next = pending.exchange(nullptr, std::memory_order_acq_rel)) {
			if (current)
				retired.push(current);  // room was checked; only this thread pushes
			current = next;
			failure.store(false, std::memory_order_release);
		}
	}
	Control control;
	while (controls.pop(control)) {
		if (control.target == current)
			current->kernel->setControl(control.slot, control.value);
	}
	if (!current)
		return;
	if (!current->started) {
		current->kernel->sampleRateChange(args.sampleRate, args.sampleTime);
		current->started = true;
	}
	if (failure.load(std::memory_order_relaxed)) {
		silence(module);
		return;
	}
	if (!current->kernel->process(module, args)) {
		failure.store(true, std::memory_order_release);
		silence(module);
		// The type's name is fixed after registration: copy it without allocating.
		char message[runtime::DIAGNOSTIC_MESSAGE_BYTES] = "kernel failed; module silenced: ";
		size_t at = std::char_traits<char>::length(message);
		for (const char* c = current->type ? current->type->name.c_str() : "?"; *c && at + 1 < sizeof(message); c++)
			message[at++] = *c;
		message[at] = '\0';
		runtime::diagnose(runtime::DiagnosticCode::KernelFailed, message);
	}
}

void KernelSlot::reset() noexcept {
	if (current) {
		current->kernel->reset();
		failure.store(false, std::memory_order_release);
	}
}

void KernelSlot::sampleRateChange(float sampleRate, float sampleTime) noexcept {
	if (current && current->started)
		current->kernel->sampleRateChange(sampleRate, sampleTime);
}

void KernelSlot::attach(std::unique_ptr<Hosted> next, bool engineRunsModule) {
	reclaim();
	latestHosted = next.get();
	if (!engineRunsModule) {
		Hosted* old = current;
		current = next.release();
		failure.store(false, std::memory_order_release);
		delete pending.exchange(nullptr);
		delete old;
		return;
	}
	// The engine never took a kernel we get back from the exchange, so it is ours to delete.
	delete pending.exchange(next.release(), std::memory_order_acq_rel);
}

bool KernelSlot::set(Hosted* target, int slot, double value, bool engineRunsModule) {
	reclaim();
	if (!engineRunsModule) {
		target->kernel->setControl(slot, value);
		return true;
	}
	// A kernel still waiting to start is the main thread's while it is out of `pending`.
	if (Hosted* waiting = pending.exchange(nullptr, std::memory_order_acq_rel)) {
		if (waiting == target)
			waiting->kernel->setControl(slot, value);
		pending.store(waiting, std::memory_order_release);
		if (waiting == target)
			return true;
	}
	return controls.push({target, slot, value});
}

void KernelSlot::reclaim() {
	Hosted* old;
	while (retired.pop(old))
		delete old;
}

// --- kernel types ------------------------------------------------------------------

namespace {

std::vector<std::unique_ptr<KernelType>>& registry() {
	static std::vector<std::unique_ptr<KernelType>> types;
	return types;
}

}  // namespace

void registerType(KernelType type) {
	if (findType(type.name))
		throw std::invalid_argument("kernel type " + type.name + " is already registered");
	registry().push_back(std::make_unique<KernelType>(std::move(type)));
}

const KernelType* findType(const std::string& name) {
	for (const auto& type : registry())
		if (type->name == name)
			return type.get();
	return nullptr;
}

std::vector<const KernelType*> types() {
	std::vector<const KernelType*> out;
	for (const auto& type : registry())
		out.push_back(type.get());
	return out;
}

void checkWiring(const KernelType& type, const std::vector<double>& values, const rack::engine::Module& module) {
	if (values.size() != type.fields.size())
		throw std::invalid_argument(type.name + " needs " + std::to_string(type.fields.size()) + " field values");
	for (size_t i = 0; i < values.size(); i++) {
		const Field& field = type.fields[i];
		double value = values[i];
		if (!std::isfinite(value))
			throw std::invalid_argument(type.name + "." + field.name + " must be finite");
		size_t count = 0;
		const char* what = nullptr;
		switch (field.kind) {
			case Field::INPUT: count = module.inputs.size(); what = "inputs"; break;
			case Field::OUTPUT: count = module.outputs.size(); what = "outputs"; break;
			case Field::PARAM: count = module.params.size(); what = "params"; break;
			case Field::LIGHT: count = module.lights.size(); what = "lights"; break;
			case Field::NUMBER: continue;
		}
		if (value != std::floor(value))
			throw std::invalid_argument(type.name + "." + field.name + " is an index and must be a whole number");
		if (value < 0) {
			if (value != -1 || field.required)
				throw std::invalid_argument(type.name + "." + field.name + " must name one of the module's " + what);
			continue;
		}
		if (value >= (double) count)
			throw std::invalid_argument(type.name + "." + field.name + " = " + std::to_string((long long) value) +
				", but the module has " + std::to_string(count) + " " + what);
	}
}

}  // namespace dsp
}  // namespace rackpy
