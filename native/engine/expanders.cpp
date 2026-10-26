// SPDX-License-Identifier: GPL-3.0-or-later
// Expander message buffers (RP-23): allocation and the address registry. See expanders.hpp.
#include "expanders.hpp"

#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace rackpy {
namespace engine {

namespace {

// Main thread in practice; the mutex only makes a misuse safe rather than racy.
std::mutex registryMutex;

std::unordered_map<const void*, MessageBuffer*>& registry() {
	static auto* buffers = new std::unordered_map<const void*, MessageBuffer*>();  // outlives static destruction
	return *buffers;
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

MessageBuffer::MessageBuffer(std::string schema, size_t size)
	: schema(std::move(schema)), size(size), data(new float[size]()) {
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
	if (!schemaName(schema))
		throw std::invalid_argument("an expander message schema is 1 to 64 letters, digits, '_', '.' or '-', not \"" + schema + "\"");
	if (size < 1 || size > MAX_MESSAGE_SIZE)
		throw std::invalid_argument("an expander message holds 1 to " + std::to_string(MAX_MESSAGE_SIZE) + " floats, not " +
			std::to_string(size));
	return std::make_shared<MessageBuffer>(schema, size);
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

}  // namespace engine
}  // namespace rackpy
