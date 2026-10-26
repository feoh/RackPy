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
// that only succeeds for RackPy modules (RP-23B).
#pragma once
#include <rack.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "rackpy/runtime/context.hpp"

namespace rackpy {
namespace module {
struct Module;
}

namespace engine {

/** The most float32 values a RackPy expander message holds. */
constexpr size_t MAX_MESSAGE_SIZE = 65536;

/** A schema name, interned: two messages have the same schema exactly when they point to the same Schema.
Never freed, so engine-thread code may keep and compare the pointer. */
struct Schema {
	const std::string name;
};

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

/** The channels a RackPy module publishes, one per side. RackPy's Module derives from it, so a cross-cast
from rack::engine::Module finds it for RackPy modules only. Written on the main thread while Rack's engine
does not run the module (RP-23A refuses slot changes otherwise), read on the engine thread. */
struct ExpanderChannels {
	Channel channels[2];
};

/** Republishes `side` of `module` from its slots: a channel when both hold registered buffers of the same
schema and size, else none. Main thread, while Rack's engine does not run the module. */
void publishChannel(rack::engine::Module& module, ExpanderChannels& channels, uint8_t side);

/** A cached cross-cast to a module's channels: recomputed only when the module pointer or its ID changes,
since a deleted module's address can be reused. Engine thread, lock-free and allocation-free. */
struct Link {
	const rack::engine::Module* module = nullptr;
	int64_t id = -1;
	ExpanderChannels* channels = nullptr;

	ExpanderChannels* resolve(rack::engine::Module* m) noexcept {
		if (m != module || (m && m->id != id)) {
			module = m;
			id = m ? m->id : -1;
			channels = m ? dynamic_cast<ExpanderChannels*>(m) : nullptr;
		}
		return channels;
	}
};

/** Where `self` sends to its neighbour on `side`: the neighbour's producer buffer for the opposite side.
Only if the neighbour is a RackPy module whose expander on that side is `self` and whose channel there has
`schema` and at least `end` values; then `flip` is that expander, whose flip the sender requests. Null
otherwise. Engine thread. */
inline float* sendBuffer(rack::engine::Module& self, uint8_t side, const Schema* schema, size_t end, Link& link,
	rack::engine::Module::Expander*& flip) noexcept {
	rack::engine::Module* neighbour = self.getExpander(side).module;
	ExpanderChannels* theirs = link.resolve(neighbour);
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
module and its own channel has `schema` and at least `end` values. Null otherwise. Engine thread. */
inline float* receiveBuffer(rack::engine::Module& self, uint8_t side, const Schema* schema, size_t end, Link& neighbour,
	Link& own) noexcept {
	rack::engine::Module::Expander& e = self.getExpander(side);
	if (!neighbour.resolve(e.module))
		return nullptr;
	ExpanderChannels* mine = own.resolve(&self);
	if (!mine)
		return nullptr;
	const Channel& c = mine->channels[side];
	if (c.schema != schema || c.size < end || !c.holds(e.consumerMessage))
		return nullptr;
	return static_cast<float*>(e.consumerMessage);
}

// --- The graph schema (RP-23B) ---------------------------------------------------------------

/** The schema of messages that DSP graph nodes exchange: lanes of GRAPH_LANE values each. */
constexpr const char* GRAPH_SCHEMA = "rackpy.graph";
/** The most lanes a graph node addresses. */
constexpr size_t MAX_GRAPH_LANES = 64;
const Schema* graphSchema();

}  // namespace engine
}  // namespace rackpy
