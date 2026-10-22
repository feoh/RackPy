// SPDX-License-Identifier: GPL-3.0-or-later
// Thread roles (ADR 003, thread rules 1, 3 and 4). Native only; no Python here.
#pragma once
#include <rack.hpp>

namespace rackpy {
namespace runtime {

enum class ThreadRole {
	/** Rack's main thread: UI, module construction and deletion, plugin init and destroy. */
	Main,
	/** A thread with a Rack context that is not the main thread: engine workers, audio and MIDI drivers. */
	Rack,
	/** A thread without a Rack context, e.g. one Python started. */
	Other,
};

const char* roleName(ThreadRole role);

/** Records the calling thread as Rack's main thread. Plugin init() calls this. */
void markMainThread();
bool isMainThread();
ThreadRole threadRole();

/** Python may run on the main thread and on threads Rack did not give a context; never on Rack's own workers. */
bool pythonAllowedHere();

/** Throws WrongThread unless this is the main thread. */
void requireMainThread(const char* operation);

/** Marks the current thread as inside a callback Rack runs under the exclusive engine lock. */
struct ExclusiveScope {
	ExclusiveScope();
	~ExclusiveScope();
	ExclusiveScope(const ExclusiveScope&) = delete;
	ExclusiveScope& operator=(const ExclusiveScope&) = delete;
};

bool insideExclusive();

/** Throws EngineLocked inside an exclusive-lock callback: the engine's lock is not recursive. */
void refuseEngineReentry(const char* operation);

/** Sets the Rack context for the current thread and restores the previous one on exit. */
struct ContextScope {
	explicit ContextScope(rack::Context* context);
	~ContextScope();
	ContextScope(const ContextScope&) = delete;
	ContextScope& operator=(const ContextScope&) = delete;

private:
	rack::Context* previous;
};

}  // namespace runtime
}  // namespace rackpy
