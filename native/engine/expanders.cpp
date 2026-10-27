// SPDX-License-Identifier: GPL-3.0-or-later
// Expander message buffers (RP-23): schemas, allocation, the address registry and channel publication.
// See expanders.hpp.
#include "expanders.hpp"

#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace rackpy {
namespace engine {

namespace {

// Main thread in practice; the mutex only makes a misuse safe rather than racy. Never on the engine thread.
std::mutex registryMutex;

std::unordered_map<const void*, MessageBuffer*>& registry() {
	static auto* buffers = new std::unordered_map<const void*, MessageBuffer*>();  // outlives static destruction
	return *buffers;
}

std::unordered_map<std::string, const Schema*>& schemas() {
	static auto* names = new std::unordered_map<std::string, const Schema*>();  // never freed: see Schema
	return *names;
}

bool schemaName(const std::string& name) {
	if (name.empty() || name.size() > 64)
		return false;
	for (char c : name)
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
			return false;
	return true;
}

}  // namespace

const Schema* internSchema(const std::string& name) {
	if (!schemaName(name))
		throw std::invalid_argument("an expander message schema is 1 to 64 letters, digits, '_', '.' or '-', not \"" + name + "\"");
	std::lock_guard<std::mutex> lock(registryMutex);
	auto found = schemas().find(name);
	if (found != schemas().end())
		return found->second;
	const Schema* schema = new Schema{name};
	schemas().emplace(name, schema);
	return schema;
}

const Schema* graphSchema() {
	static const Schema* schema = internSchema(GRAPH_SCHEMA);
	return schema;
}

MessageBuffer::MessageBuffer(const Schema* schema, size_t size)
	: schema(schema), size(size), data(new float[size]()) {
	std::lock_guard<std::mutex> lock(registryMutex);
	registry()[data] = this;
}

MessageBuffer::~MessageBuffer() {
	{
		std::lock_guard<std::mutex> lock(registryMutex);
		registry().erase(data);
	}
	delete[] data;
}

std::shared_ptr<MessageBuffer> allocateMessage(const std::string& schema, size_t size) {
	const Schema* interned = internSchema(schema);
	if (size < 1 || size > MAX_MESSAGE_SIZE)
		throw std::invalid_argument("an expander message holds 1 to " + std::to_string(MAX_MESSAGE_SIZE) + " floats, not " +
			std::to_string(size));
	return std::make_shared<MessageBuffer>(interned, size);
}

MessageBuffer* findMessage(const void* pointer) {
	if (!pointer)
		return nullptr;
	std::lock_guard<std::mutex> lock(registryMutex);
	auto found = registry().find(pointer);
	return found == registry().end() ? nullptr : found->second;
}

size_t messageCount() {
	std::lock_guard<std::mutex> lock(registryMutex);
	return registry().size();
}

void publishChannel(rack::engine::Module& module, ExpanderChannels& channels, uint8_t side) {
	side = side ? 1 : 0;
	const rack::engine::Module::Expander& e = module.getExpander(side);
	MessageBuffer* a = findMessage(e.producerMessage);
	MessageBuffer* b = findMessage(e.consumerMessage);
	Channel c;
	if (a && b && a != b && a->schema == b->schema && a->size == b->size)
		c = Channel{a->schema, a->size, {a->data, b->data}};
	Channel& old = channels.channels[side];
	bool same = old.schema == c.schema && old.size == c.size &&
		((old.buffers[0] == c.buffers[0] && old.buffers[1] == c.buffers[1]) ||
			(old.buffers[0] == c.buffers[1] && old.buffers[1] == c.buffers[0]));
	old = c;
	if (!same)
		channels.snapshots[side] = c.schema ? std::make_unique<Snapshot>(c.size) : nullptr;
}

namespace {

/** What kernel libraries call: the same checks as the plan interpreter's. */
struct HostServices final : expander::Services {
	const Schema* schema(const char* name) noexcept override {
		try {
			return name ? internSchema(name) : nullptr;
		}
		catch (...) {
			return nullptr;
		}
	}
	float* send(rack::engine::Module& self, uint8_t side, const Schema* schema, size_t size, Link& link) noexcept override {
		rack::engine::Module::Expander* flip = nullptr;
		float* message = sendBuffer(self, side ? 1 : 0, schema, size, link, flip);
		if (message)
			flip->requestMessageFlip();
		return message;
	}
	float* receive(rack::engine::Module& self, uint8_t side, const Schema* schema, size_t size, int64_t frame,
		Link& neighbour, Link& own) noexcept override {
		return receiveBuffer(self, side ? 1 : 0, schema, size, frame, neighbour, own);
	}
};

}  // namespace

expander::Services& hostServices() {
	static HostServices services;
	return services;
}

}  // namespace engine
}  // namespace rackpy
