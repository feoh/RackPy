// SPDX-License-Identifier: GPL-3.0-or-later
#include "rackpy/runtime/context.hpp"

#include "rackpy/runtime/diagnostics.hpp"

namespace rackpy {
namespace runtime {

namespace {

std::atomic<uint64_t> generations{0};

uint64_t nextGeneration() {
	return generations.fetch_add(1, std::memory_order_relaxed) + 1;
}

}  // namespace

ContextToken::ContextToken(rack::Context* context) : context(context), generation(nextGeneration()) {}

std::shared_ptr<ContextToken> ContextBinding::observe(rack::Context* context) {
	if (!context)
		return nullptr;
	std::lock_guard<std::mutex> lock(mutex);
	if (!bound) {
		bound = std::make_shared<ContextToken>(context);
		INFO("RackPy bound to Rack context %p (generation %llu)", (void*) context, (unsigned long long) bound->generation);
		return bound;
	}
	if (bound->context == context && bound->alive())
		return bound;
	if (refused.fetch_add(1) == 0) {
		diagnose(DiagnosticCode::ForeignContext, "a second Rack context asked for Python work; RackPy supports one context per process");
		WARN("RackPy refused Rack context %p: it is bound to %p, and several contexts in one process are not supported",
			(void*) context, (void*) bound->context);
	}
	return nullptr;
}

std::shared_ptr<ContextToken> ContextBinding::current() const {
	std::lock_guard<std::mutex> lock(mutex);
	return bound && bound->alive() ? bound : nullptr;
}

void ContextBinding::close() {
	std::shared_ptr<ContextToken> token;
	{
		std::lock_guard<std::mutex> lock(mutex);
		token = bound;
	}
	if (!token || !token->alive())
		return;
	// In-flight engine calls hold the lease shared; wait for them, then refuse new ones.
	std::unique_lock<std::shared_mutex> drain(token->lease);
	token->live.store(false, std::memory_order_release);
}

void ContextBinding::reset() {
	close();
	std::lock_guard<std::mutex> lock(mutex);
	bound.reset();
}

ContextBinding& binding() {
	static ContextBinding instance;
	return instance;
}

void checkContext(const ContextToken& token) {
	if (!token.alive())
		throw StaleHandle("the Rack context this handle belongs to has closed");
	rack::Context* active = rack::contextGet();
	if (active && active != token.context)
		throw WrongContext("this handle belongs to a different Rack context than the one active on this thread");
}

EngineLease::EngineLease(ContextToken& token) : hold(token.lease), scope(token.context) {
	if (!token.alive())
		throw StaleHandle("the Rack context has closed");
}

ObjectToken::ObjectToken(std::shared_ptr<ContextToken> context) : generation(nextGeneration()), context(std::move(context)) {}

}  // namespace runtime
}  // namespace rackpy
