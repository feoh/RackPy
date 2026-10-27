// SPDX-License-Identifier: GPL-3.0-or-later
// A module's MIDI input (RP-31J): Rack's InputQueue (src/midi.cpp) for the
// engine thread, without its mutex or its heap. Python-free.
//
// Rack hands a MIDI input its messages on the MIDI driver's thread, on an
// engine thread for its Loopback driver, or on Python's thread for a device
// Python drives, so several producers may push at once: a bounded lock-free
// queue (Vyukov's, one sequence number per cell) takes them, dropping and
// counting what finds it full. Once per frame, before the module's kernel
// runs, the engine thread moves what arrived into a fixed buffer kept in
// (frame, arrival) order, the order of Rack's priority queue, and delivers
// those whose frame is due (at or before ProcessArgs::frame), as
// InputQueue::tryPop does. At most 8192 messages wait, Rack's limit, and a
// message carries at most 3 bytes: longer ones (SysEx) are dropped and
// counted.
#pragma once
#include <rack.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "rackpy/io.hpp"

namespace rackpy {
namespace io {

/** Rack's InputQueue_maxSize: the most messages waiting for their frame. */
constexpr size_t MIDI_QUEUE_CAPACITY = 8192;

/** Counters, written on the producers' and engine threads, read by Python. */
struct MidiStats {
	std::atomic<uint64_t> received{0};  // messages queued
	std::atomic<uint64_t> droppedFull{0};  // refused: MIDI_QUEUE_CAPACITY messages were waiting
	std::atomic<uint64_t> droppedLong{0};  // refused: more than MAX_MIDI_BYTES bytes (SysEx)
	std::atomic<uint64_t> delivered{0};  // given to the kernel at their frames
};

/** One MIDI input of a module. Made and deleted on the main thread. */
class MidiQueue {
public:
	MidiQueue();
	MidiQueue(const MidiQueue&) = delete;
	MidiQueue& operator=(const MidiQueue&) = delete;
	MidiStats stats;

	/** Any thread: queues `message`, or drops and counts it. Never blocks or allocates. */
	void push(const rack::midi::Message& message) noexcept;

	// --- the engine thread, once per module frame ---
	/** The previous frame's messages go; what arrived since joins the waiting ones in order; the due ones are
	due() and dueCount() until the next call. */
	void collect(int64_t frame) noexcept;
	const MidiMessage* due() const noexcept { return pending.get() + begin; }
	int dueCount() const noexcept { return (int) dueEnd; }
	/** Messages waiting for a later frame, as of the last collect(). */
	size_t waiting() const noexcept { return held.load(std::memory_order_relaxed); }

private:
	struct Cell {
		std::atomic<size_t> sequence;
		MidiMessage message;
	};
	std::unique_ptr<Cell[]> cells;
	std::atomic<size_t> enqueuePos{0};
	std::atomic<size_t> dequeuePos{0};  // advanced by the engine thread only
	// The engine thread's (frame, arrival)-ordered buffer, from `begin`, `count` long; the first `dueEnd` are due.
	std::unique_ptr<MidiMessage[]> pending;
	size_t begin = 0, count = 0, dueEnd = 0;
	std::atomic<size_t> held{0};  // `count`, for the producers' limit
	bool pop(MidiMessage& out) noexcept;
	void insert(const MidiMessage& m) noexcept;
};

}  // namespace io
}  // namespace rackpy
