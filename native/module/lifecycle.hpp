// SPDX-License-Identifier: GPL-3.0-or-later
// Lifecycle event snapshots (RP-14C3). Python-free: Rack can dispatch these
// events on an audio driver thread (sample rate, master changes), so recording
// one takes no lock, allocates nothing, logs nothing and never calls Python.
#pragma once
#include <rack.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace rackpy {
namespace module {

enum class LifecycleKind : uint8_t {
	Add,
	Remove,
	Bypass,
	UnBypass,
	PortChange,
	Reset,
	Randomize,
	SampleRateChange,
	Save,
	SetMaster,
	UnsetMaster,
};

/** One event as Rack dispatched it, with the event's own fields copied. */
struct LifecycleRecord {
	uint64_t sequence = 0;  // 1, 2, 3, ... per module, including dropped events
	LifecycleKind kind = LifecycleKind::Add;
	float sampleRate = 0.f;  // SampleRateChange
	float sampleTime = 0.f;
	bool connecting = false;  // PortChange
	rack::engine::Port::Type portType = rack::engine::Port::INPUT;
	int portId = 0;
};

/** A bounded single-producer, single-consumer ring. Rack's engine lock (or the
main thread, for a module nothing else owns) serializes the producers; the
consumer is Python on the main thread. A full ring drops the new event and
counts it: the producer never waits. */
class LifecycleRing {
public:
	static constexpr size_t CAPACITY = 64;

	void push(LifecycleRecord record) noexcept;
	/** Copy out up to `max` records, oldest first, and release their slots. */
	size_t drain(LifecycleRecord* out, size_t max) noexcept;
	uint64_t dropped() const noexcept { return droppedCount.load(std::memory_order_relaxed); }

private:
	std::array<LifecycleRecord, CAPACITY> slots{};
	std::atomic<uint64_t> head{0};  // written by the producer
	std::atomic<uint64_t> tail{0};  // written by the consumer
	std::atomic<uint64_t> sequence{0};
	std::atomic<uint64_t> droppedCount{0};
};

}  // namespace module
}  // namespace rackpy
