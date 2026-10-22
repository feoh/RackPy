// SPDX-License-Identifier: GPL-3.0-or-later
// Rack context binding, generation tokens and borrowed handles (ADR 003,
// decisions 6, 9, 12 and 13). Native only: deleting a Rack object or closing a
// context flips atomics and does no Python work, so it is safe on any thread.
#pragma once
#include <rack.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace runtime {

/** One Rack context as RackPy sees it. Generations are never reused, even if a context's address is. */
struct ContextToken {
	rack::Context* const context;
	const uint64_t generation;

	explicit ContextToken(rack::Context* context);
	bool alive() const { return live.load(std::memory_order_acquire); }

	/** Held shared by every engine call from Python; teardown takes it exclusively (EngineLease). */
	std::shared_mutex lease;
	std::atomic<bool> live{true};
};

/** RackPy's single bound context (ADR 003, decision 12) and its teardown. */
class ContextBinding {
public:
	/** Binds `context` if nothing is bound yet. Returns its token, or null for any other context
	(which is refused, with a diagnostic the first time). Main thread. */
	std::shared_ptr<ContextToken> observe(rack::Context* context);

	/** The bound context's token, or null before binding and after teardown. */
	std::shared_ptr<ContextToken> current() const;

	/** Waits for in-flight engine calls, then marks the bound context dead. Idempotent. */
	void close();

	/** Forget the binding entirely (plugin destroy()). */
	void reset();

	uint64_t refusedContexts() const { return refused.load(); }

private:
	mutable std::mutex mutex;
	std::shared_ptr<ContextToken> bound;
	std::atomic<uint64_t> refused{0};
};

ContextBinding& binding();

/** Throws StaleHandle if `token` is dead, WrongContext if it is not the context active on this thread. */
void checkContext(const ContextToken& token);

/** A shared hold on a live context for the length of an engine call; throws StaleHandle if it is gone.
Taken with the GIL released (ADR 003, decision 2). Also scopes the context for the calling thread. */
class EngineLease {
public:
	explicit EngineLease(ContextToken& token);
	EngineLease(const EngineLease&) = delete;
	EngineLease& operator=(const EngineLease&) = delete;

private:
	std::shared_lock<std::shared_mutex> hold;
	ContextScope scope;
};

/** Lifetime of one Rack-owned object. The object's owner calls invalidate() when it is deleted. */
struct ObjectToken {
	const uint64_t generation;
	const std::shared_ptr<ContextToken> context;

	explicit ObjectToken(std::shared_ptr<ContextToken> context);
	bool alive() const { return live.load(std::memory_order_acquire) && (!context || context->alive()); }
	void invalidate() { live.store(false, std::memory_order_release); }

private:
	std::atomic<bool> live{true};
};

/** A Python-side view of a Rack-owned T: never owns it, never outlives it silently. */
template <typename T>
struct Borrowed {
	T* object = nullptr;
	std::shared_ptr<ObjectToken> token;
	const char* type = "object";

	/** The object, if it and its context are alive; otherwise throws StaleHandle. */
	T& get() const {
		if (!token || !token->alive()) {
			if (token && token->context && !token->context->alive())
				throw StaleHandle(rack::string::f("this %s belonged to a Rack context that has closed", type));
			throw StaleHandle(rack::string::f("this %s has been deleted by Rack", type));
		}
		return *object;
	}
	bool alive() const { return token && token->alive(); }
	uint64_t generation() const { return token ? token->generation : 0; }
};

}  // namespace runtime
}  // namespace rackpy
