// SPDX-License-Identifier: GPL-3.0-or-later
#include "rackpy/runtime/python.hpp"

#include <rack.hpp>

#include <map>
#include <mutex>
#include <string>
#include <utility>

#include "rackpy/runtime/errors.hpp"

namespace rackpy {
namespace runtime {

namespace {

std::atomic<bool> alive{false};
std::atomic<uint64_t> calls{0};
std::atomic<uint64_t> refusedRackThread{0};
std::atomic<uint64_t> raised{0};
std::atomic<size_t> liveRefs{0};
std::atomic<size_t> transferred{0};

// Deferred releases: a fixed array of slots claimed with compare-and-swap. No lock, no allocation.
constexpr size_t RELEASE_CAPACITY = 4096;
std::atomic<PyObject*> releaseSlots[RELEASE_CAPACITY] = {};
std::atomic<size_t> releaseHint{0};
std::atomic<size_t> releasePending{0};

// Identity: (address, generation) -> weak reference to the wrapper. Touched only with the GIL.
using IdentityKey = std::pair<const void*, uint64_t>;
std::map<IdentityKey, py::object>* identities = nullptr;

std::map<std::string, PyObject*> errorTypes;

}  // namespace

bool interpreterAlive() {
	return alive.load(std::memory_order_acquire);
}

void setInterpreterAlive(bool value) {
	alive.store(value, std::memory_order_release);
}

DispatchStats dispatchStats() {
	return {calls.load(), refusedRackThread.load(), raised.load()};
}

namespace detail {

void noteRefused(const char* what) {
	// May be an audio thread: count and record without allocating or logging.
	refusedRackThread.fetch_add(1, std::memory_order_relaxed);
	diagnose(DiagnosticCode::RefusedRackThread, what);
}

void noteRaised(const char* what, const char* message) {
	raised.fetch_add(1, std::memory_order_relaxed);
	std::string text = std::string(what) + ": " + (message ? message : "");
	diagnose(DiagnosticCode::CallbackRaised, text.c_str());
	// RP-38: bounded log output. A callback that raises every frame (a widget's step or draw) must not grow
	// Rack's log without limit. Every raise is counted and recorded above; the log gets the first
	// RAISED_LOG_BURST of them, then at most one a second, which says how many it left out.
	static constexpr uint64_t RAISED_LOG_BURST = 20;
	static std::mutex mutex;  // Python runs only on Rack's main thread; this is for safety, never contended
	static uint64_t logged = 0, omitted = 0;
	static double lastLogged = 0;
	static bool noticed = false;
	std::lock_guard<std::mutex> lock(mutex);
	double now = rack::system::getTime();
	if (logged >= RAISED_LOG_BURST && now - lastLogged < 1.0) {
		if (!noticed) {
			WARN("RackPy: Python keeps raising; from now on its errors are logged at most once a second. "
				"rackpy.runtime.diagnostics() records the latest %d, and rackpy's dispatch counts count all",
				(int) DIAGNOSTIC_CAPACITY);
			noticed = true;
		}
		omitted++;
		return;
	}
	logged++;
	lastLogged = now;
	if (omitted) {
		WARN("RackPy: Python raised in %s: %s (and %llu more since the last such message; "
			"rackpy.runtime.diagnostics() has the latest)", what, message ? message : "", (unsigned long long) omitted);
		omitted = 0;
	}
	else
		WARN("RackPy: Python raised in %s: %s", what, message ? message : "");
}

void noteCall() {
	calls.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace detail

void releaseLater(PyObject* object) {
	if (!object)
		return;
	size_t start = releaseHint.fetch_add(1, std::memory_order_relaxed);
	for (size_t i = 0; i < RELEASE_CAPACITY; i++) {
		PyObject* expected = nullptr;
		if (releaseSlots[(start + i) % RELEASE_CAPACITY].compare_exchange_strong(expected, object, std::memory_order_release)) {
			releasePending.fetch_add(1, std::memory_order_release);
			return;
		}
	}
	diagnose(DiagnosticCode::ReleaseQueueFull, "deferred-release queue full; one Python object leaked");
}

size_t pendingReleases() {
	return releasePending.load(std::memory_order_acquire);
}

size_t drainReleases() {
	if (releasePending.load(std::memory_order_acquire) == 0)
		return 0;
	size_t released = 0;
	for (auto& slot : releaseSlots) {
		PyObject* object = slot.exchange(nullptr, std::memory_order_acquire);
		if (object) {
			releasePending.fetch_sub(1, std::memory_order_relaxed);
			Py_DECREF(object);
			released++;
		}
	}
	return released;
}

PyRef::PyRef(py::object object) : ptr(object.release().ptr()) {
	if (ptr)
		liveRefs.fetch_add(1, std::memory_order_relaxed);
}

PyRef::PyRef(PyRef&& other) noexcept : ptr(std::exchange(other.ptr, nullptr)) {}

PyRef& PyRef::operator=(PyRef&& other) noexcept {
	if (this != &other) {
		reset();
		ptr = std::exchange(other.ptr, nullptr);
	}
	return *this;
}

PyRef::~PyRef() {
	reset();
}

py::object PyRef::get() const {
	return ptr ? py::reinterpret_borrow<py::object>(ptr) : py::none();
}

void PyRef::reset() {
	PyObject* object = std::exchange(ptr, nullptr);
	if (!object)
		return;
	liveRefs.fetch_sub(1, std::memory_order_relaxed);
	if (!interpreterAlive())
		return;  // after finalization, or after RackPy refused to finalize: leak rather than touch Python
	if (PyGILState_Check() && pythonAllowedHere())
		Py_DECREF(object);
	else
		releaseLater(object);  // e.g. destroyed on an engine thread: the main thread drops it later
}

size_t nativeReferences() {
	return liveRefs.load(std::memory_order_relaxed);
}

void noteTransferred() {
	transferred.fetch_add(1, std::memory_order_relaxed);
}

void noteTransferredDeleted() {
	transferred.fetch_sub(1, std::memory_order_relaxed);
}

size_t transferredObjects() {
	return transferred.load(std::memory_order_relaxed);
}

py::object identity(const void* object, uint64_t generation, const std::function<py::object()>& make) {
	if (!identities)
		identities = new std::map<IdentityKey, py::object>();
	IdentityKey key{object, generation};
	auto it = identities->find(key);
	if (it != identities->end()) {
		py::object wrapper = it->second();  // call the weak reference
		if (!wrapper.is_none())
			return wrapper;
		identities->erase(it);
	}
	py::object wrapper = make();
	// When the wrapper is collected, forget it. The callback runs with the GIL.
	py::cpp_function forget([key](py::handle) {
		if (identities)
			identities->erase(key);
	});
	(*identities)[key] = py::weakref(wrapper, forget);
	return wrapper;
}

size_t identityEntries() {
	return identities ? identities->size() : 0;
}

void clearIdentities() {
	if (!identities)
		return;
	auto* doomed = identities;
	identities = nullptr;  // weak-reference callbacks fired during deletion find nothing to erase
	delete doomed;
}

namespace {

PyObject* errorType(const char* name) {
	auto it = errorTypes.find(name);
	if (it != errorTypes.end())
		return it->second;
	try {
		py::object type = py::module_::import("rackpy.errors").attr(name);
		PyObject* ptr = type.release().ptr();
		errorTypes[name] = ptr;
		return ptr;
	}
	catch (py::error_already_set&) {
		return PyExc_RuntimeError;
	}
}

}  // namespace

void registerErrorTranslator() {
	py::register_exception_translator([](std::exception_ptr p) {
		try {
			if (p)
				std::rethrow_exception(p);
		}
		catch (const StaleHandle& e) {
			PyErr_SetString(errorType("StaleHandleError"), e.what());
		}
		catch (const WrongContext& e) {
			PyErr_SetString(errorType("ContextError"), e.what());
		}
		catch (const WrongThread& e) {
			PyErr_SetString(errorType("ThreadError"), e.what());
		}
		catch (const Ownership& e) {
			PyErr_SetString(errorType("OwnershipError"), e.what());
		}
		catch (const EngineLocked& e) {
			PyErr_SetString(errorType("EngineLockError"), e.what());
		}
		catch (const Laboratory& e) {
			PyErr_SetString(errorType("LaboratoryError"), e.what());
		}
		catch (const py::cast_error& e) {
			// RP-38: a Python value of the wrong type met an explicit py::cast (a plan, an event field, a JSON key).
			// pybind11 reports it as RuntimeError; Python, and pybind11's own argument checks, say TypeError.
			PyErr_SetString(PyExc_TypeError, e.what());
		}
	});
}

void dropErrorTypes() {
	for (auto& entry : errorTypes)
		Py_DECREF(entry.second);
	errorTypes.clear();
}

}  // namespace runtime
}  // namespace rackpy
