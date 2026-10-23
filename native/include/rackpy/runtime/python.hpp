// SPDX-License-Identifier: GPL-3.0-or-later
// The runtime's Python side: the one way Rack calls into Python, references
// that native objects hold, deferred releases, object identity and error
// translation (ADR 003, decisions 1, 7, 9 and 11).
#pragma once
#include <pybind11/pybind11.h>

#include <atomic>
#include <cstdint>
#include <functional>

#include "rackpy/runtime/diagnostics.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace runtime {

namespace py = pybind11;

/** True between a successful interpreter start and the beginning of finalization. */
bool interpreterAlive();
void setInterpreterAlive(bool alive);

struct DispatchStats {
	uint64_t calls;  // Python callbacks that ran
	uint64_t refusedRackThread;  // callbacks refused because Rack called on one of its own threads
	uint64_t raised;  // callbacks that raised, caught at the boundary
};
DispatchStats dispatchStats();

namespace detail {
void noteRefused(const char* what);
void noteRaised(const char* what, const char* message);
void noteCall();
}  // namespace detail

/** Release every queued Python reference. Needs the GIL; returns how many were released. */
size_t drainReleases();

/** Queue one Python reference to be released on the main thread. Any thread, lock-free.
Steals the reference. If the queue is full the object leaks, with a diagnostic. */
void releaseLater(PyObject* object);
size_t pendingReleases();

/** Run Python for Rack: refuses Rack's own threads, takes the GIL, and catches everything,
so nothing propagates into Rack. Returns false if `f` did not run to completion. */
template <typename F>
bool callPython(const char* what, F&& f) {
	if (!interpreterAlive())
		return false;
	if (!pythonAllowedHere()) {
		detail::noteRefused(what);
		return false;
	}
	PyGILState_STATE gil = PyGILState_Ensure();
	bool ok = false;
	try {
		detail::noteCall();
		f();
		ok = true;
	}
	catch (py::error_already_set& e) {
		detail::noteRaised(what, e.what());
	}
	catch (const std::exception& e) {
		detail::noteRaised(what, e.what());
	}
	catch (...) {
		detail::noteRaised(what, "unknown C++ exception");
	}
	drainReleases();
	PyGILState_Release(gil);
	return ok;
}

/** A strong reference that a native object holds to a Python object. It may be destroyed on any
thread: without the GIL it queues the release for the main thread instead of touching Python. */
class PyRef {
public:
	PyRef() = default;
	/** Needs the GIL. */
	explicit PyRef(py::object object);
	PyRef(PyRef&& other) noexcept;
	PyRef& operator=(PyRef&& other) noexcept;
	PyRef(const PyRef&) = delete;
	PyRef& operator=(const PyRef&) = delete;
	~PyRef();

	/** A new reference to the object. Needs the GIL. */
	py::object get() const;
	explicit operator bool() const { return ptr != nullptr; }
	/** The object without a new reference, for a GC traverse function only. */
	PyObject* borrowed() const { return ptr; }
	void reset();

private:
	PyObject* ptr = nullptr;
};

/** How many PyRefs currently hold a Python object. */
size_t nativeReferences();

/** Python-created objects handed to Rack (py::classh with trampoline_self_life_support) keep their
Python half alive until Rack deletes them, and their destructor takes the GIL. The binding that
transfers one calls noteTransferred(); the object's destructor calls noteTransferredDeleted().
finalize() refuses to finalize while any are alive. */
void noteTransferred();
void noteTransferredDeleted();
size_t transferredObjects();

/** The one Python wrapper for (object, generation), created by `make` if there is none alive.
Repeated views of the same Rack object are therefore `is`-identical. Needs the GIL. */
py::object identity(const void* object, uint64_t generation, const std::function<py::object()>& make);
size_t identityEntries();
void clearIdentities();

/** Translate the runtime's C++ exceptions into rackpy.errors. Call once, with the GIL. */
void registerErrorTranslator();
/** Drop the cached exception classes. Needs the GIL; call before Py_FinalizeEx. */
void dropErrorTypes();

}  // namespace runtime
}  // namespace rackpy
