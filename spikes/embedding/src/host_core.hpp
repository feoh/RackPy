// SPDX-License-Identifier: GPL-3.0-or-later
// Shared host core for the RP-04 embedding spike. Spike code, not production.
#pragma once
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <rack.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "host_api.h"

namespace spike {

/** State shared between a Rack-owned module and the Python handles that borrow it.
Python holds the token, never the module, so deleting the module needs no Python work.
*/
struct Token {
	std::atomic<bool> alive{true};
	std::atomic<float> gain{1.f};
	std::atomic<uint64_t> processed{0};
	std::atomic<float> lastOut{0.f};
	std::atomic<int64_t> moduleId{-1};
};

/** Raised by handle accessors after the module is gone; bindings map it to ReferenceError. */
struct StaleHandle {
	int64_t moduleId;
};

/** Borrowed view of a Rack-owned SpikeModule. */
struct Handle {
	std::shared_ptr<Token> token;

	const Token& live() const {
		if (!token->alive.load())
			throw StaleHandle{token->moduleId.load()};
		return *token;
	}
	int64_t id() const { return token->moduleId.load(); }
	bool alive() const { return token->alive.load(); }
	float gain() const { return live().gain.load(); }
	void setGain(float g) {
		live();
		token->gain.store(g);
	}
	uint64_t processed() const { return live().processed.load(); }
	/** Copy of native state; the returned buffer owns its storage. */
	std::vector<float> snapshot() const { return std::vector<float>(16, live().lastOut.load()); }
};

/** What each binding library provides. Every function runs with the GIL held and must not throw. */
struct BindingOps {
	const char* name;
	/** Before Py_InitializeFromConfig. */
	bool (*registerInittab)();
	/** Import the trusted script; returns false and fills err on failure. */
	bool (*afterInit)(const std::string& script, std::string& err);
	/** UI-thread callback from the ModuleWidget constructor. */
	void (*onWidgetCreated)(const Handle& handle);
	/** Drop every Python object C++ holds, before Py_FinalizeEx. */
	void (*beforeFinalize)();
};

const BindingOps& bindingOps();

/** Parent widget of the ModuleWidget being configured, or NULL outside that callback. */
rack::widget::Widget* currentParent();

int hostInit(RackPySpikeHostInit* args);
void hostDestroy();

}  // namespace spike

extern "C" int rackpy_spike_host_init(RackPySpikeHostInit* args);
extern "C" void rackpy_spike_host_destroy();
