// SPDX-License-Identifier: GPL-3.0-or-later
// Keeping the laboratory's drivers and devices alive while Rack's code uses them, and destroying them only on
// the main thread (RP-22F). See laboratory.hpp.
#include <mutex>
#include <utility>
#include <vector>

#include "laboratory.hpp"
#include "../bindings/midi.hpp"
#include "rackpy/runtime/interpreter.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace laboratory {

namespace {

std::vector<runtime::PyRef> kept;  // main thread
int calls = 0;                     // LaboratoryCalls in progress (main thread)
int dispatching = 0;               // subscription overrides in progress (main thread)

std::mutex laterMutex;
std::vector<std::function<void()>> later;  // destructions requested off the main thread
bool hooked = false;

bool busy() {
	return calls > 0 || dispatching > 0 || bindings::midiDeliveryInProgress();
}

void deleteLater() noexcept {
	for (;;) {
		std::vector<std::function<void()>> due;
		{
			std::lock_guard<std::mutex> lock(laterMutex);
			due.swap(later);
		}
		if (due.empty())
			return;
		for (auto& deleter : due) {
			try {
				deleter();
			}
			catch (...) {
			}
		}
	}
}

/** At the UI step and context close: under the GIL, outside every Rack call that could use the objects. */
void maintain() noexcept {
	bool pending;
	{
		std::lock_guard<std::mutex> lock(laterMutex);
		pending = !later.empty();
	}
	if ((kept.empty() && !pending) || busy())
		return;
	runtime::callPython("releasing laboratory drivers and devices", [] {
		releaseKeptLaboratoryObjects();
		deleteLater();
	});
}

void hook() {
	if (hooked)
		return;
	hooked = true;
	runtime::onUiStep([] { maintain(); });
	runtime::onContextClosing([] { maintain(); });
	runtime::onFinalize([] {  // the GIL is held here
		releaseKeptLaboratoryObjects();
		deleteLater();
	});
}

}  // namespace

void keepLaboratoryObjectsAlive(py::object extra) {
	hook();
	std::vector<py::object> live;
	liveMidiObjects(live);
	liveAudioObjects(live);
	if (extra)
		live.push_back(std::move(extra));
	for (auto& object : live)
		kept.emplace_back(std::move(object));
}

void releaseKeptLaboratoryObjects() noexcept {
	// Destructors may run Python that keeps more alive: release that too, while nothing is in progress.
	while (!busy() && !kept.empty()) {
		std::vector<runtime::PyRef> drop;
		drop.swap(kept);
		drop.clear();
	}
}

LaboratoryCall::LaboratoryCall() {
	++calls;
}

LaboratoryCall::~LaboratoryCall() {
	--calls;
	releaseKeptLaboratoryObjects();
}

SubscriptionDispatch::SubscriptionDispatch() {
	++dispatching;
}

SubscriptionDispatch::~SubscriptionDispatch() {
	--dispatching;  // still inside Rack's port code: what was kept is released later
}

void deleteOnMainThreadLater(std::function<void()> deleter) {
	if (runtime::isMainThread()) {
		deleter();
		return;
	}
	std::lock_guard<std::mutex> lock(laterMutex);
	later.push_back(std::move(deleter));
}

void bindKeepAlive() {
	hook();  // the deferred destructions need the hooks even if nothing was ever kept
}

}  // namespace laboratory
}  // namespace rackpy
