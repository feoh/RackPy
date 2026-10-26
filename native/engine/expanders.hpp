// SPDX-License-Identifier: GPL-3.0-or-later
// Expander message buffers (RP-23): RackPy's allocations for the void*
// double buffers of Rack's Module::Expander. Python-free.
//
// Rack leaves a module to allocate both buffers of an expander side and to
// check its neighbour before reinterpreting the neighbour's void*. RackPy
// allocates buffers with a schema (a name and a count of float32 values) on
// the main thread, and recognizes them afterwards by looking their address up
// in this registry, never by reading the memory a pointer points to: a
// foreign plugin's pointer is not found, and stays opaque.
#pragma once
#include <cstddef>
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

/** One RackPy expander message buffer: `size` float32 values, zeroed, under a schema name. */
struct MessageBuffer : std::enable_shared_from_this<MessageBuffer> {
	const std::string schema;
	const size_t size;
	float* const data;
	/** The RackPy module whose expander slots hold it, while one does (main thread). */
	module::Module* owner = nullptr;
	std::shared_ptr<runtime::ObjectToken> ownerToken;

	MessageBuffer(std::string schema, size_t size);
	~MessageBuffer();
	MessageBuffer(const MessageBuffer&) = delete;
	MessageBuffer& operator=(const MessageBuffer&) = delete;

	/** The module that holds it, if that module still lives. */
	module::Module* liveOwner() const { return owner && ownerToken && ownerToken->alive() ? owner : nullptr; }
};

/** Checks the schema name (1 to 64 characters: letters, digits, '_', '.', '-') and the size (1 to
MAX_MESSAGE_SIZE) and allocates a registered buffer. Throws std::invalid_argument. Main thread. */
std::shared_ptr<MessageBuffer> allocateMessage(const std::string& schema, size_t size);

/** The RackPy buffer whose data is at `pointer`, or null for any other pointer (which is never
dereferenced). Main thread. */
MessageBuffer* findMessage(const void* pointer);

/** How many buffers are registered (tests). */
size_t messageCount();

}  // namespace engine
}  // namespace rackpy
