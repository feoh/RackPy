// SPDX-License-Identifier: GPL-3.0-or-later
// rack::SharedMutex and rack::SharedLock -> rackpy.raw (RP-30C). Rack's
// mutex wraps a pthread rwlock and asserts on every error: unlocking a lock
// the thread does not hold, or locking it again for writing, would stop
// Rack. So RackPy's mutex records which threads hold it, refuses those
// misuses before pthread sees them, and releases the GIL while it waits (a
// Python thread holding it may need the GIL to let go). SharedLock<TMutex>
// takes RackPy's SharedMutex natively or, for any other TMutex, an object
// with lock_shared() and unlock_shared(), called with the GIL and no native
// lock held. Locks are never for Rack's audio path; Python never runs there.
#include <pybind11/pybind11.h>

#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include <rack.hpp>

#include "services.hpp"

namespace rackpy {
namespace bindings {
namespace services {

namespace py = pybind11;

namespace {

/** Rack's SharedMutex with its holders recorded. */
struct TrackedMutex {
	rack::SharedMutex mutex;
	std::mutex book;
	std::thread::id writer;
	bool written = false;
	std::map<std::thread::id, int> readers;

	~TrackedMutex() = default;
	bool heldByMe() {
		std::lock_guard<std::mutex> lock(book);
		auto me = std::this_thread::get_id();
		return (written && writer == me) || readers.count(me);
	}
	void lock() {
		if (heldByMe())
			throw py::value_error("this thread holds the mutex already: locking it again would deadlock");
		{
			py::gil_scoped_release release;
			mutex.lock();
		}
		std::lock_guard<std::mutex> lock(book);
		writer = std::this_thread::get_id();
		written = true;
	}
	bool tryLock() {
		if (heldByMe())
			throw py::value_error("this thread holds the mutex already");
		if (!mutex.try_lock())
			return false;
		std::lock_guard<std::mutex> lock(book);
		writer = std::this_thread::get_id();
		written = true;
		return true;
	}
	void unlock() {
		{
			std::lock_guard<std::mutex> lock(book);
			if (!written || writer != std::this_thread::get_id())
				throw py::value_error("this thread does not hold the mutex for writing");
			written = false;
		}
		mutex.unlock();
	}
	void lockShared() {
		if (heldByMe())
			throw py::value_error("this thread holds the mutex already: Rack's rwlock would deadlock or fail");
		{
			py::gil_scoped_release release;
			mutex.lock_shared();
		}
		std::lock_guard<std::mutex> lock(book);
		readers[std::this_thread::get_id()] = 1;
	}
	bool tryLockShared() {
		if (heldByMe())
			throw py::value_error("this thread holds the mutex already");
		if (!mutex.try_lock_shared())
			return false;
		std::lock_guard<std::mutex> lock(book);
		readers[std::this_thread::get_id()] = 1;
		return true;
	}
	void unlockShared() {
		{
			std::lock_guard<std::mutex> lock(book);
			auto it = readers.find(std::this_thread::get_id());
			if (it == readers.end())
				throw py::value_error("this thread does not hold the mutex shared");
			readers.erase(it);
		}
		mutex.unlock_shared();
	}
	bool locked() {
		std::lock_guard<std::mutex> lock(book);
		return written || !readers.empty();
	}
};

/** Destroying a held rwlock is undefined: a mutex still held when Python drops it is never destroyed (a leak). */
struct MutexDelete {
	void operator()(TrackedMutex* m) const noexcept {
		if (m->locked()) {
			WARN("RackPy: a SharedMutex was dropped while held; it is leaked rather than destroyed");
			return;
		}
		delete m;
	}
};

/** Rack's SharedLock: holds the mutex shared from construction until release (or the with block's end). */
struct PySharedLock {
	std::shared_ptr<TrackedMutex> mutex;  // RackPy's SharedMutex, held natively; null for any other object
	py::object owner;  // the object it holds: the SharedMutex, or the object with lock_shared/unlock_shared
	std::thread::id locker;  // the thread that locked owner
	bool held = false;
	PySharedLock(std::shared_ptr<TrackedMutex> m, py::object o)
		: mutex(std::move(m)), owner(std::move(o)), locker(std::this_thread::get_id()), held(true) {}
	PySharedLock(const PySharedLock&) = delete;
	PySharedLock& operator=(const PySharedLock&) = delete;
	void release() {
		if (!held)
			return;
		if (mutex) {
			mutex->unlockShared();
			held = false;
			return;
		}
		// Released before the call, so another thread's release() while unlock_shared runs is a no-op.
		held = false;
		try {
			owner.attr("unlock_shared")();
		}
		catch (...) {
			held = true;  // unlock_shared failed: the object still counts as held
			throw;
		}
	}
	// Dropped while held: let go as C++ RAII would at scope end, if this thread holds it. Python runs here
	// (with the GIL, from the collector) only on the locking thread and never while the interpreter finalizes.
	~PySharedLock() {
		if (!held)
			return;
		if (mutex) {
			try {
				mutex->unlockShared();
			}
			catch (...) {
			}
			return;
		}
		if (Py_IsFinalizing()) {
			WARN("RackPy: a SharedLock was dropped while held during Python's shutdown; unlock_shared was not called");
			return;
		}
		if (locker != std::this_thread::get_id()) {
			PyErr_SetString(PyExc_RuntimeError,
				"a SharedLock was dropped while held on a thread that did not lock it; unlock_shared was not called");
			PyErr_WriteUnraisable(owner.ptr());
			return;
		}
		try {
			owner.attr("unlock_shared")();
		}
		catch (py::error_already_set& e) {
			e.discard_as_unraisable(owner);
		}
		catch (...) {
		}
	}
};

}  // namespace

void bindMutex(py::module_ raw) {
	py::class_<TrackedMutex, std::shared_ptr<TrackedMutex>>(raw, "SharedMutex",
		"Rack's SharedMutex (a pthread rwlock): many readers or one writer. RackPy refuses a thread locking it twice or "
		"unlocking what it does not hold (Rack would assert), and releases the GIL while it waits.")
		.def(py::init([] { return std::shared_ptr<TrackedMutex>(new TrackedMutex, MutexDelete{}); }))
		.def("lock", &TrackedMutex::lock, "Rack's: waits to hold it for writing.")
		.def("try_lock", &TrackedMutex::tryLock, "Rack's: holds it for writing if it can at once; whether it did.")
		.def("unlock", &TrackedMutex::unlock)
		.def("lock_shared", &TrackedMutex::lockShared, "Rack's: waits to hold it for reading, with other readers.")
		.def("try_lock_shared", &TrackedMutex::tryLockShared)
		.def("unlock_shared", &TrackedMutex::unlockShared)
		.def("__enter__", [](py::object self) {
			self.cast<TrackedMutex&>().lock();
			return self;
		})
		.def("__exit__", [](TrackedMutex& m, py::args) { m.unlock(); }, "with mutex: holds it for writing in the block.");

	py::class_<PySharedLock, std::unique_ptr<PySharedLock>>(raw, "SharedLock",
		"Rack's SharedLock<TMutex>: holds m shared from construction until release() or the with block's end "
		"(C++ RAII, made explicit). m is a SharedMutex or any object with lock_shared() and unlock_shared().")
		.def(py::init([](py::object m) {
			if (py::type::of(m).is(py::type::of<TrackedMutex>())) {
				auto tracked = m.cast<std::shared_ptr<TrackedMutex>>();
				tracked->lockShared();
				return new PySharedLock(tracked, m);
			}
			// Any other TMutex, a Python subclass of SharedMutex included: Rack's m.lock_shared(), in Python.
			py::object lock = py::getattr(m, "lock_shared", py::none());
			py::object unlock = py::getattr(m, "unlock_shared", py::none());
			if (!PyCallable_Check(lock.ptr()) || !PyCallable_Check(unlock.ptr()))
				throw py::type_error("SharedLock takes a SharedMutex or an object with lock_shared() and unlock_shared(), not " +
					py::str(py::type::of(m).attr("__qualname__")).cast<std::string>());
			lock();
			return new PySharedLock(nullptr, m);
		}), py::arg("m"))
		.def_property_readonly("m", [](const PySharedLock& l) { return l.owner; }, "Rack's: the mutex it holds.")
		.def_property_readonly("held", [](const PySharedLock& l) { return l.held; })
		.def("release", &PySharedLock::release,
			"Rack's ~SharedLock: lets the mutex go (once). An error from unlock_shared propagates and m stays held.")
		.def("__enter__", [](py::object self) { return self; })
		.def("__exit__", [](PySharedLock& l, py::args) { l.release(); });
}

}  // namespace services
}  // namespace bindings
}  // namespace rackpy
