// SPDX-License-Identifier: GPL-3.0-or-later
// Native lifecycle adapters of rackpy::module::Module (RP-14C3). This file must
// not reference CPython: tests/native checks its object with
// `bin/rackpy-dev audio-symbols`, because Rack may run some of these handlers on
// an audio driver thread.
#include "lifecycle.hpp"
#include "module.hpp"

namespace rackpy {
namespace module {

void LifecycleRing::push(LifecycleRecord record) noexcept {
	record.sequence = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
	uint64_t h = head.load(std::memory_order_relaxed);
	if (h - tail.load(std::memory_order_acquire) >= CAPACITY) {
		droppedCount.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	slots[h % CAPACITY] = record;
	head.store(h + 1, std::memory_order_release);
}

size_t LifecycleRing::drain(LifecycleRecord* out, size_t max) noexcept {
	uint64_t t = tail.load(std::memory_order_relaxed);
	uint64_t h = head.load(std::memory_order_acquire);
	size_t n = 0;
	for (; t < h && n < max; t++, n++)
		out[n] = slots[t % CAPACITY];
	tail.store(t, std::memory_order_release);
	return n;
}

namespace {

LifecycleRecord recordOf(LifecycleKind kind) {
	LifecycleRecord record;
	record.kind = kind;
	return record;
}

}  // namespace

// Each handler keeps Rack's superclass behaviour first (the default handlers
// reset/randomize parameters and call the deprecated no-argument events), then
// records what happened. Rack holds its engine lock exclusively for every event
// here except onSave (shared), so the ui-exclusive ones mark the thread: Python
// that a quantity override runs inside them may not take the engine lock.

void Module::onAdd(const AddEvent& e) {
	runtime::ExclusiveScope exclusive;
	rack::engine::Module::onAdd(e);
	handle(recordOf(LifecycleKind::Add));
}

void Module::onRemove(const RemoveEvent& e) {
	runtime::ExclusiveScope exclusive;
	rack::engine::Module::onRemove(e);
	handle(recordOf(LifecycleKind::Remove));
}

void Module::onBypass(const BypassEvent& e) {
	runtime::ExclusiveScope exclusive;
	rack::engine::Module::onBypass(e);
	handle(recordOf(LifecycleKind::Bypass));
}

void Module::onUnBypass(const UnBypassEvent& e) {
	runtime::ExclusiveScope exclusive;
	rack::engine::Module::onUnBypass(e);
	handle(recordOf(LifecycleKind::UnBypass));
}

void Module::onPortChange(const PortChangeEvent& e) {
	runtime::ExclusiveScope exclusive;
	rack::engine::Module::onPortChange(e);
	LifecycleRecord record = recordOf(LifecycleKind::PortChange);
	record.connecting = e.connecting;
	record.portType = e.type;
	record.portId = e.portId;
	handle(record);
}

void Module::onReset(const ResetEvent& e) {
	runtime::ExclusiveScope exclusive;
	rack::engine::Module::onReset(e);
	kernels.reset();
	handle(recordOf(LifecycleKind::Reset));
}

void Module::onRandomize(const RandomizeEvent& e) {
	runtime::ExclusiveScope exclusive;
	rack::engine::Module::onRandomize(e);
	handle(recordOf(LifecycleKind::Randomize));
}

// Driver events: possibly an audio driver thread, under the exclusive lock.
void Module::onSampleRateChange(const SampleRateChangeEvent& e) {
	runtime::ExclusiveScope exclusive;
	rack::engine::Module::onSampleRateChange(e);
	kernels.sampleRateChange(e.sampleRate, e.sampleTime);
	LifecycleRecord record = recordOf(LifecycleKind::SampleRateChange);
	record.sampleRate = e.sampleRate;
	record.sampleTime = e.sampleTime;
	lifecycle.push(record);
}

void Module::onSetMaster(const SetMasterEvent& e) {
	runtime::ExclusiveScope exclusive;
	rack::engine::Module::onSetMaster(e);
	lifecycle.push(recordOf(LifecycleKind::SetMaster));
}

void Module::onUnsetMaster(const UnsetMasterEvent& e) {
	runtime::ExclusiveScope exclusive;
	rack::engine::Module::onUnsetMaster(e);
	lifecycle.push(recordOf(LifecycleKind::UnsetMaster));
}

// An engine event: Rack's stepBlock updates expander pointers on the thread
// stepping the engine, and removeModule on the main thread. Snapshot only.
void Module::onExpanderChange(const ExpanderChangeEvent& e) {
	rack::engine::Module::onExpanderChange(e);
	LifecycleRecord record = recordOf(LifecycleKind::ExpanderChange);
	record.side = e.side;
	lifecycle.push(record);
}

// Custom state: Rack saves under the shared lock (audio runs on), loads under
// the exclusive one. Without handlers, Rack's defaults: no data, nothing to load.
json_t* Module::dataToJson() {
	return dataHandlers ? dataHandlers->toJson() : rack::engine::Module::dataToJson();
}

void Module::dataFromJson(json_t* rootJ) {
	runtime::ExclusiveScope exclusive;
	if (dataHandlers)
		dataHandlers->fromJson(rootJ);
	else
		rack::engine::Module::dataFromJson(rootJ);
}

// Shared lock: audio keeps running.
void Module::onSave(const SaveEvent& e) {
	rack::engine::Module::onSave(e);
	handle(recordOf(LifecycleKind::Save));
}

}  // namespace module
}  // namespace rackpy
