// SPDX-License-Identifier: GPL-3.0-or-later
// Expander message buffers (RP-23): RackPy's allocations for the void*
// double buffers of Rack's Module::Expander, and the lock-free checks native
// code makes before it uses a neighbour's buffers. Python-free.
//
// Rack leaves a module to allocate both buffers of an expander side and to
// check its neighbour before reinterpreting the neighbour's void*. RackPy
// allocates buffers with a schema (a name and a count of float32 values) on
// the main thread, and recognizes them afterwards by looking their address up
// in this registry, never by reading the memory a pointer points to: a
// foreign plugin's pointer is not found, and stays opaque.
//
// On Rack's engine thread the registry (which takes a mutex) is never used.
// Each RackPy module instead publishes, per side, the schema, size and two
// buffers its slots hold (a Channel), on the main thread while Rack's engine
// does not run it; native code finds a neighbour's channels by a cross-cast
// that only succeeds for RackPy modules (RP-23B). Kernel libraries reach the
// same checks through rackpy/expanders.hpp's Services (RP-23C).
#pragma once
#include <rack.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rackpy/expanders.hpp"
#include "rackpy/runtime/context.hpp"

namespace rackpy {
namespace module {
struct Module;
}

namespace expander {
/** See rackpy/expanders.hpp: two messages have the same schema exactly when they point to the same Schema. */
struct Schema {
	const std::string name;
};
}  // namespace expander

namespace engine {

/** The most float32 values a RackPy expander message holds. */
constexpr size_t MAX_MESSAGE_SIZE = 65536;

/** A schema name, interned. Never freed, so engine-thread code may keep and compare the pointer. */
using Schema = expander::Schema;

/** Checks the name (1 to 64 characters: letters, digits, '_', '.', '-') and interns it. Throws
std::invalid_argument. Main thread. */
const Schema* internSchema(const std::string& name);

/** One RackPy expander message buffer: `size` float32 values, zeroed, under a schema. */
struct MessageBuffer : std::enable_shared_from_this<MessageBuffer> {
	const Schema* const schema;
	const size_t size;
	float* const data;
	/** The RackPy module whose expander slots hold it, while one does (main thread). */
	module::Module* owner = nullptr;
	std::shared_ptr<runtime::ObjectToken> ownerToken;

	MessageBuffer(const Schema* schema, size_t size);
	~MessageBuffer();
	MessageBuffer(const MessageBuffer&) = delete;
	MessageBuffer& operator=(const MessageBuffer&) = delete;

	/** The module that holds it, if that module still lives. */
	module::Module* liveOwner() const { return owner && ownerToken && ownerToken->alive() ? owner : nullptr; }
};

/** Checks the schema name and the size (1 to MAX_MESSAGE_SIZE) and allocates a registered buffer. Throws
std::invalid_argument. Main thread. */
std::shared_ptr<MessageBuffer> allocateMessage(const std::string& schema, size_t size);

/** The RackPy buffer whose data is at `pointer`, or null for any other pointer (which is never
dereferenced). Main thread. */
MessageBuffer* findMessage(const void* pointer);

/** How many buffers are registered (tests). */
size_t messageCount();

// --- Channels: what native code may use on Rack's engine thread ----------------------------

/** One side of a RackPy module's expander, as native code sees it: the two RackPy buffers its slots hold,
which Rack's engine swaps, under one schema and size; or no schema when the slots do not hold such a pair. */
struct Channel {
	const Schema* schema = nullptr;
	size_t size = 0;
	float* buffers[2] = {nullptr, nullptr};
	bool holds(const void* pointer) const { return pointer && (pointer == buffers[0] || pointer == buffers[1]); }
};

/** The last message a side received, for Python to read while Rack's engine runs the module (RP-23C): a
sequence lock, written by the engine thread at most once per frame, read on the main thread. */
struct Snapshot {
	explicit Snapshot(size_t size) : size(size), values(new std::atomic<float>[size]) {
		for (size_t i = 0; i < size; i++)
			values[i].store(0.f, std::memory_order_relaxed);
	}
	const size_t size;
	std::atomic<uint32_t> sequence{0};  // odd while being written
	std::atomic<int64_t> frame{-1};  // the frame it was received in; -1 for none yet
	std::unique_ptr<std::atomic<float>[]> values;

	/** Engine thread: copies `message` once per frame. The values are release stores after the odd
	sequence store, so a reader that sees any of them also sees the sequence change (no fences, which
	ThreadSanitizer does not model). */
	void write(const float* message, int64_t at) noexcept {
		if (frame.load(std::memory_order_relaxed) == at)
			return;
		uint32_t s = sequence.load(std::memory_order_relaxed);
		sequence.store(s + 1, std::memory_order_relaxed);
		for (size_t i = 0; i < size; i++)
			values[i].store(message[i], std::memory_order_release);
		frame.store(at, std::memory_order_release);
		sequence.store(s + 2, std::memory_order_release);
	}
	/** Main thread: a consistent copy, or false if the engine thread kept writing through `attempts` tries.
	Acquire loads keep the second sequence load after the values. */
	bool read(int64_t& at, std::vector<float>& out, int attempts = 1000) const {
		out.resize(size);
		for (int k = 0; k < attempts; k++) {
			uint32_t before = sequence.load(std::memory_order_acquire);
			if (before & 1u)
				continue;
			for (size_t i = 0; i < size; i++)
				out[i] = values[i].load(std::memory_order_acquire);
			at = frame.load(std::memory_order_acquire);
			if (sequence.load(std::memory_order_relaxed) == before)
				return true;
		}
		return false;
	}
};

/** The channels a RackPy module publishes, one per side. RackPy's Module derives from it, so a cross-cast
from rack::engine::Module finds it for RackPy modules only. Written on the main thread while Rack's engine
does not run the module (RP-23A refuses slot changes otherwise), read on the engine thread. */
struct ExpanderChannels {
	Channel channels[2];
	/** Whether Python asked for snapshots of each side (main thread writes, engine thread reads). */
	std::atomic<bool> snapshotsWanted[2] = {{false}, {false}};
	/** Allocated with the channel, replaced only while Rack's engine does not run the module. */
	std::unique_ptr<Snapshot> snapshots[2];
};

/** Republishes `side` of `module` from its slots: a channel when both hold registered buffers of the same
schema and size, else none, with a new snapshot when the channel changed. Main thread, while Rack's engine
does not run the module. */
void publishChannel(rack::engine::Module& module, ExpanderChannels& channels, uint8_t side);

/** A cached neighbour lookup (rackpy/expanders.hpp's Link, filled in here). */
using Link = expander::Link;

/** The channels of `m` through `link`'s cross-cast, recomputed only when the module pointer or its ID
changes, since a deleted module's address can be reused. Engine thread, lock-free and allocation-free. */
inline ExpanderChannels* resolve(Link& link, rack::engine::Module* m) noexcept {
	if (m != link.module || (m && m->id != link.id)) {
		link.module = m;
		link.id = m ? m->id : -1;
		link.channels = m ? dynamic_cast<ExpanderChannels*>(m) : nullptr;
	}
	return static_cast<ExpanderChannels*>(link.channels);
}

/** Where `self` sends to its neighbour on `side`: the neighbour's producer buffer for the opposite side.
Only if the neighbour is a RackPy module whose expander on that side is `self` and whose channel there has
`schema` and at least `end` values; then `flip` is that expander, whose flip the sender requests. Null
otherwise. Engine thread. */
inline float* sendBuffer(rack::engine::Module& self, uint8_t side, const Schema* schema, size_t end, Link& link,
	rack::engine::Module::Expander*& flip) noexcept {
	rack::engine::Module* neighbour = self.getExpander(side).module;
	ExpanderChannels* theirs = resolve(link, neighbour);
	if (!theirs)
		return nullptr;
	uint8_t back = side ? 0 : 1;
	rack::engine::Module::Expander& e = neighbour->getExpander(back);
	const Channel& c = theirs->channels[back];
	if (e.module != &self || c.schema != schema || c.size < end || !c.holds(e.producerMessage))
		return nullptr;
	flip = &e;
	return static_cast<float*>(e.producerMessage);
}

/** What `self` receives on `side`: its own consumer buffer there, if its neighbour on that side is a RackPy
module and its own channel has `schema` and at least `end` values. Null otherwise. When Python asked for
snapshots of the side, the message is copied to its snapshot the first time in frame `frame`, before the
caller marks anything read. Engine thread. */
inline float* receiveBuffer(rack::engine::Module& self, uint8_t side, const Schema* schema, size_t end, int64_t frame,
	Link& neighbour, Link& own) noexcept {
	rack::engine::Module::Expander& e = self.getExpander(side);
	if (!resolve(neighbour, e.module))
		return nullptr;
	ExpanderChannels* mine = resolve(own, &self);
	if (!mine)
		return nullptr;
	const Channel& c = mine->channels[side];
	if (c.schema != schema || c.size < end || !c.holds(e.consumerMessage))
		return nullptr;
	float* message = static_cast<float*>(e.consumerMessage);
	if (mine->snapshotsWanted[side].load(std::memory_order_relaxed) && mine->snapshots[side])
		mine->snapshots[side]->write(message, frame);
	return message;
}

/** RackPy's implementation of rackpy/expanders.hpp's Services, which kernel libraries receive. */
expander::Services& hostServices();

// --- The graph schema (RP-23B) ---------------------------------------------------------------

/** The schema of messages that DSP graph nodes exchange: lanes of GRAPH_LANE values each. */
constexpr const char* GRAPH_SCHEMA = "rackpy.graph";
/** The most lanes a graph node addresses. */
constexpr size_t MAX_GRAPH_LANES = 64;
const Schema* graphSchema();

}  // namespace engine
}  // namespace rackpy
