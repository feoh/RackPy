// SPDX-License-Identifier: GPL-3.0-or-later
#include "rackpy/runtime/threads.hpp"

#include <atomic>
#include <thread>

#include "rackpy/runtime/errors.hpp"

namespace rackpy {
namespace runtime {

namespace {

std::atomic<std::thread::id> mainThread{};
thread_local int exclusiveDepth = 0;

}  // namespace

const char* roleName(ThreadRole role) {
	switch (role) {
		case ThreadRole::Main: return "main";
		case ThreadRole::Rack: return "rack";
		case ThreadRole::Other: return "other";
	}
	return "?";
}

void markMainThread() {
	mainThread.store(std::this_thread::get_id());
}

bool isMainThread() {
	return std::this_thread::get_id() == mainThread.load();
}

ThreadRole threadRole() {
	if (isMainThread())
		return ThreadRole::Main;
	// contextGet() is thread-local; Rack's engine workers and driver callbacks set it themselves.
	return rack::contextGet() ? ThreadRole::Rack : ThreadRole::Other;
}

bool pythonAllowedHere() {
	return threadRole() != ThreadRole::Rack;
}

void requireMainThread(const char* operation) {
	if (isMainThread())
		return;
	const char* here = threadRole() == ThreadRole::Rack ? "one of Rack's engine or driver threads" : "a thread Rack did not start";
	throw WrongThread(rack::string::f("%s must be called on Rack's main (UI) thread, not on %s", operation, here));
}

ExclusiveScope::ExclusiveScope() {
	exclusiveDepth++;
}

ExclusiveScope::~ExclusiveScope() {
	exclusiveDepth--;
}

bool insideExclusive() {
	return exclusiveDepth > 0;
}

void refuseEngineReentry(const char* operation) {
	if (insideExclusive())
		throw EngineLocked(rack::string::f("%s takes the engine lock, which Rack already holds exclusively during this "
			"callback; calling it here would deadlock", operation));
}

ContextScope::ContextScope(rack::Context* context) : previous(rack::contextGet()) {
	rack::contextSet(context);
}

ContextScope::~ContextScope() {
	rack::contextSet(previous);
}

}  // namespace runtime
}  // namespace rackpy
