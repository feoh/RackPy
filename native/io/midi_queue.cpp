// SPDX-License-Identifier: GPL-3.0-or-later
// A module's MIDI input (RP-31J): see midi_queue.hpp.
#include "midi_queue.hpp"

#include <cstring>

namespace rackpy {
namespace io {

static_assert((MIDI_QUEUE_CAPACITY & (MIDI_QUEUE_CAPACITY - 1)) == 0, "a power of two");
static_assert(std::atomic<size_t>::is_always_lock_free, "MIDI queue indices must be lock-free");
static_assert(std::atomic<uint64_t>::is_always_lock_free, "MIDI counters must be lock-free");
static_assert(std::atomic<MidiQueue*>::is_always_lock_free, "MIDI queue handover must be lock-free");

MidiQueue::MidiQueue() : cells(new Cell[MIDI_QUEUE_CAPACITY]), pending(new MidiMessage[MIDI_QUEUE_CAPACITY]) {
	for (size_t i = 0; i < MIDI_QUEUE_CAPACITY; i++)
		cells[i].sequence.store(i, std::memory_order_relaxed);
}

void MidiQueue::push(const rack::midi::Message& message) noexcept {
	size_t size = message.bytes.size();
	if (size > (size_t) MAX_MIDI_BYTES) {
		stats.droppedLong.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	// Reserve against the total, not separate snapshots of the ring and held
	// messages: concurrent producers must not jointly exceed Rack's limit.
	size_t used = outstanding.load(std::memory_order_relaxed);
	for (;;) {
		if (used >= MIDI_QUEUE_CAPACITY) {
			stats.droppedFull.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (outstanding.compare_exchange_weak(used, used + 1, std::memory_order_relaxed))
			break;
	}
	size_t pos = enqueuePos.load(std::memory_order_relaxed);
	for (;;) {
		Cell& cell = cells[pos & (MIDI_QUEUE_CAPACITY - 1)];
		size_t sequence = cell.sequence.load(std::memory_order_acquire);
		intptr_t diff = (intptr_t) sequence - (intptr_t) pos;
		if (diff == 0) {
			if (enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
				cell.message.frame = message.frame;
				cell.message.size = (uint8_t) size;
				for (size_t i = 0; i < size; i++)
					cell.message.bytes[i] = message.bytes[i];
				cell.sequence.store(pos + 1, std::memory_order_release);
				stats.received.fetch_add(1, std::memory_order_relaxed);
				return;
			}
		}
		else if (diff < 0) {
			outstanding.fetch_sub(1, std::memory_order_relaxed);
			stats.droppedFull.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		else {
			pos = enqueuePos.load(std::memory_order_relaxed);
		}
	}
}

bool MidiQueue::pop(MidiMessage& out) noexcept {
	size_t pos = dequeuePos.load(std::memory_order_relaxed);
	Cell& cell = cells[pos & (MIDI_QUEUE_CAPACITY - 1)];
	if (cell.sequence.load(std::memory_order_acquire) != pos + 1)
		return false;
	out = cell.message;
	cell.sequence.store(pos + MIDI_QUEUE_CAPACITY, std::memory_order_release);
	dequeuePos.store(pos + 1, std::memory_order_release);
	return true;
}

void MidiQueue::insert(const MidiMessage& m) noexcept {
	if (begin + count == MIDI_QUEUE_CAPACITY) {
		std::memmove(pending.get(), pending.get() + begin, count * sizeof(MidiMessage));
		begin = 0;
	}
	// After every waiting message of the same or an earlier frame: Rack's priority queue breaks ties by arrival.
	MidiMessage* first = pending.get() + begin;
	size_t at = count;
	while (at > 0 && first[at - 1].frame > m.frame) {
		first[at] = first[at - 1];
		at--;
	}
	first[at] = m;
	count++;
}

void MidiQueue::collect(int64_t frame) noexcept {
	begin += dueEnd;
	count -= dueEnd;
	dueEnd = 0;
	if (count == 0)
		begin = 0;
	MidiMessage m;
	while (count < MIDI_QUEUE_CAPACITY && pop(m))
		insert(m);
	const MidiMessage* first = pending.get() + begin;
	while (dueEnd < count && first[dueEnd].frame <= frame)
		dueEnd++;
	held.store(count - dueEnd, std::memory_order_relaxed);
	if (dueEnd) {
		outstanding.fetch_sub(dueEnd, std::memory_order_relaxed);
		stats.delivered.fetch_add(dueEnd, std::memory_order_relaxed);
	}
}

}  // namespace io
}  // namespace rackpy
