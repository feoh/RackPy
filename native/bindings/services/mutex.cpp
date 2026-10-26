// SPDX-License-Identifier: GPL-3.0-or-later
// rack::SharedMutex and rack::SharedLock -> rackpy.raw (RP-30C). Rack's
// mutex wraps a pthread rwlock and asserts on every error: unlocking a lock
// the thread does not hold, or locking it again for writing, would stop
// Rack. So RackPy's mutex records which threads hold it, refuses those
// misuses before pthread sees them, and releases the GIL while it waits (a
// Python thread holding it may need the GIL to let go). Locks are never for
// Rack's audio path; Python never runs there.
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
	std::shared_ptr<TrackedMutex> mutex;
	py::object owner;
	bool held = false;
	PySharedLock(std::shared_ptr<TrackedMutex> m, py::object o) : mutex(std::move(m)), owner(std::move(o)), held(true) {}
	PySharedLock(const PySharedLock&) = delete;
	PySharedLock& operator=(const PySharedLock&) = delete;
	// Dropped while held: let go as C++ RAII would at scope end, if this thread holds it.
	~PySharedLock() {
		if (held && mutex) {
			try {
				mutex->unlockShared();
			}
			catch (...) {
			}
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
		"Rack's SharedLock<TMutex>: holds a SharedMutex shared from construction until release() or the with block's end "
		"(C++ RAII, made explicit).")
		.def(py::init([](py::object mutex) {
			auto m = mutex.cast<std::shared_ptr<TrackedMutex>>();
			m->lockShared();
			return new PySharedLock(m, mutex);
		}), py::arg("m"))
		.def_property_readonly("m", [](const PySharedLock& l) { return l.owner; }, "Rack's: the mutex it holds.")
		.def_property_readonly("held", [](const PySharedLock& l) { return l.held; })
		.def("release", [](PySharedLock& l) {
			if (!l.held)
				return;
			l.mutex->unlockShared();
			l.held = false;
		}, "Rack's ~SharedLock: lets the mutex go (once).")
		.def("__enter__", [](py::object self) { return self; })
		.def("__exit__", [](PySharedLock& l, py::args) {
			if (l.held) {
				l.mutex->unlockShared();
				l.held = false;
			}
		});
}

}  // namespace services
}  // namespace bindings
}  // namespace rackpy
