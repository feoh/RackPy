// SPDX-License-Identifier: GPL-3.0-or-later
// The CPython owner: isolated start-up in plugin init(), the shutdown order of
// ADR 003 (decision 13), and finalization in plugin destroy().
#pragma once
#include <functional>
#include <memory>
#include <string>

#include "rackpy/runtime/context.hpp"
#include "rackpy/host_abi.h"

namespace rackpy {
namespace runtime {

/** Starts the interpreter and imports rackpy._bootstrap. Returns 0, or a nonzero code with `args->error` set.
On failure the caller must still call finalize(). */
int start(RackPyHostInit* args);

/** Finalizes the interpreter, or leaves it running (never finalizing it unsafely) if native objects still
hold Python references after every shutdown hook has run. */
void finalize();

/** Binds the Rack context active on this thread, if RackPy has none yet, and installs the teardown
sentinel in its scene. Main thread. Returns the token, or null for a refused or missing context.
Module and module-widget constructors call this. */
std::shared_ptr<ContextToken> bindCurrentContext();

/** The earliest teardown signal (ADR 003, decision 13, steps 1 to 3): runs the Python context-closing
hooks, then drains in-flight engine calls and marks the context dead. Main thread; idempotent. */
void beginContextShutdown(const char* source);

/** Register a function that drops the Python references a native component holds (callback
registries and the like). finalize() runs them, newest first, with the GIL held, before it checks
that no native object still holds Python references. Main thread. */
void onFinalize(std::function<void()> detach);

/** Native UI cleanup only: no engine/audio callers. Functions must not throw.
UI steps reclaim detached objects whose last handle died on another thread;
context-closing hooks run after Python workers stop and before EventState dies. */
void onUiStep(std::function<void()> action);
void onContextClosing(std::function<void()> action);

/** Graphics lifetime is shorter than Context lifetime: an embedded host can
recreate its window. Hooks run after scene event delivery while the old/new
window context is still current, without treating recreation as shutdown. */
void onGraphicsContextDestroy(std::function<void()> action);
void onGraphicsContextCreate(std::function<void()> action);

/** How finalize() ended, for tests: "finalized", "refused" or "not started". */
std::string finalizeOutcome();

}  // namespace runtime
}  // namespace rackpy
