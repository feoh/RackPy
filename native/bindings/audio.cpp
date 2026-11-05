// SPDX-License-Identifier: GPL-3.0-or-later
// rack::audio -> rackpy.raw.audio (RP-29B): Rack's audio drivers and devices
// as checked views, and Python-owned audio ports.
//
// Threads: a device calls its ports' processInput, processBuffer and
// processOutput on the audio driver's thread, and onStartStream and
// onStopStream there or wherever the port subscribes. Python never runs on
// that thread: on_start_stream and on_stop_stream reach a Python subclass
// queued, on the UI thread, and the process callbacks run natively (Rack's
// no-op) while the port counts what passed, for Python to read. Only a
// device that Python itself drives (RP-29C) calls a Python port's process
// overrides, synchronously, on Python's thread.
//
// A port that is a RackPy module's audio port (Module.set_audio_port, RP-31J)
// runs Core Audio's buffering natively instead (native/io/audio_bridge.hpp):
// its device's frames reach the module's kernel on Rack's engine thread, and
// the kernel's frames the device, through lock-free rings.
//
// Lifetimes: Rack's audio drivers live until audio::destroy(), which runs
// before RackPy finalizes Python, so every Python port gives its device back
// when Rack's context closes. A device belongs to its driver (RtAudio deletes
// one when its last port leaves), so a device view goes through its port.
//
// The callback laboratory's driver and devices (RP-22F, native/laboratory/
// audio.cpp) are Unlisted ones: Rack never lists them, so only Python's ports
// reach them, and the ports' teardown here leaves them without running Python.
#include "rackpy/runtime/buffers.hpp"
#include "rackpy/rtti.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <map>
#include <set>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <rack.hpp>

#include "audio.hpp"
#include "engine/module.hpp"
#include "../io/endpoints.hpp"
#include "../laboratory/laboratory.hpp"
#include "engine/engine_internal.hpp"
#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/interpreter.hpp"
#include "rackpy/runtime/json.hpp"
#include "rackpy/runtime/python.hpp"
#include "rackpy/runtime/threads.hpp"
#include "support/overrides.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;
namespace a = rack::audio;

namespace {

enum class StreamEvent : uint8_t { Start, Stop };

/** Set while a device that Python drives (PythonDevice) runs Rack's Device code on Python's thread. */
thread_local bool pythonDriving = false;
/** The devices whose Rack code (processBuffer, the stream callbacks) Python is running on this thread. Rack holds a
device's processMutex in processBuffer and iterates its ports in each: a Python port's override that changed those
ports, or waited for that mutex, would deadlock or break Rack's iteration (RP-42). */
struct Driven {
	const a::Device* device;
	bool locked;  // in processBuffer, which holds processMutex (the stream callbacks do not)
};
thread_local std::vector<Driven> drivenDevices;
thread_local std::vector<const a::Device*> processMutexGuards;
bool drivenHere(const a::Device& d, bool lockedOnly) {
	return std::any_of(drivenDevices.begin(), drivenDevices.end(),
		[&](const Driven& x) { return x.device == &d && (x.locked || !lockedOnly); });
}
bool processMutexGuardedHere(const a::Device& d) {
	return std::find(processMutexGuards.begin(), processMutexGuards.end(), &d) != processMutexGuards.end();
}
void forgetProcessMutexGuard(const a::Device* device) {
	auto it = std::find(processMutexGuards.begin(), processMutexGuards.end(), device);
	if (it != processMutexGuards.end())
		processMutexGuards.erase(it);
}

/** Refuses a call that changes which devices a port holds while a device's callbacks run on this thread. */
void refuseWhileDriving(const char* what) {
	if (!drivenDevices.empty())
		throw runtime::Ownership(std::string(what) + " is refused inside an audio device's process or stream "
			"callbacks: Rack is iterating that device's ports (and holds its processMutex) on this thread");
}

/** Locks a device's processMutex, never waiting for it with the GIL held (a Python-driven processBuffer on another
thread holds it while a Python port's override waits for the GIL), nor for the GIL with it held: it returns holding
the GIL if the caller did. Refused on a thread that already holds it. */
std::unique_lock<std::mutex> lockProcess(a::Device& d, const char* what) {
	if (processMutexGuardedHere(d))
		throw runtime::Ownership(std::string(what) + " is refused while this thread holds the device's process_mutex guard");
	if (drivenHere(d, true))
		throw runtime::Ownership(std::string(what) + " is refused inside this device's own process or stream "
			"callbacks, where Rack holds its processMutex on this thread");
	bool gil = runtime::interpreterAlive() && PyGILState_Check();
	for (;;) {
		std::unique_lock<std::mutex> lock(d.processMutex, std::try_to_lock);
		if (lock.owns_lock() || !gil) {
			if (!lock.owns_lock())
				lock.lock();
			return lock;
		}
		py::gil_scoped_release release;  // wait for the holder without the GIL, then try again with it
		std::lock_guard<std::mutex> wait(d.processMutex);
	}
}

/** Copies `count` floats into a new array('f'). */
py::object floatArray(const float* data, size_t count) {
	return py::module_::import("array").attr("array")("f", py::bytes(reinterpret_cast<const char*>(data), count * sizeof(float)));
}

/** Copies an array('f') (or any float32 buffer) back into `data`, up to `count` floats. */
void copyBack(py::handle array, float* data, size_t count) {
	py::buffer_info info = py::reinterpret_borrow<py::buffer>(array).request();
	if (info.itemsize != sizeof(float) || info.format != py::format_descriptor<float>::format())
		throw py::type_error("the output must stay a float32 buffer");
	runtime::requireContiguous(info, "the output");
	size_t n = std::min(count, static_cast<size_t>(info.size));
	std::copy_n(static_cast<const float*>(info.ptr), n, data);
}

/** A Python audio port: Rack's Port, with its stream events queued for the UI thread. */
struct PyAudioPort : a::Port {
	bool released = false;
	struct Direct {
		int driverId;
		int deviceId;
		a::Device* device;
	};
	std::vector<Direct> driverSubscriptions;      // Driver.subscribe
	std::vector<a::Device*> deviceSubscriptions;  // Device.subscribe
	// Counted natively on the audio thread, read by Python.
	std::atomic<uint64_t> buffers{0}, frames{0}, inputCalls{0}, outputCalls{0};
	// RP-31J: the module whose audio port this is. The driver reads `bridge`; the main thread sets and clears it
	// under the device's processMutex, which Rack's Device holds while it calls the port, so the driver is never in
	// the bridge then. The module clears it (Endpoints::detachAudio) before it goes; `bridgeModule` lives while set.
	std::atomic<io::AudioBridge*> bridge{nullptr};
	module::Module* bridgeModule = nullptr;  // main thread, and the driver while `bridge` is set
	std::shared_ptr<io::AudioBridge> bridgeOwner;  // main thread
	void detachBridge() noexcept;
	std::atomic<int> lastFrames{0};
	// Stream events, from whichever thread Rack calls them on; never blocks it.
	std::mutex eventMutex;
	std::vector<StreamEvent> events;
	std::atomic<uint64_t> droppedEvents{0};

	PyAudioPort();
	~PyAudioPort() override;
	void release() noexcept;
	bool holds(const a::Device* device) const {
		if (device && device == this->device)
			return true;
		for (auto& d : driverSubscriptions)
			if (d.device == device)
				return true;
		return std::find(deviceSubscriptions.begin(), deviceSubscriptions.end(), device) != deviceSubscriptions.end();
	}

	// The process callbacks: native counting on Rack's audio thread; a Python override only on a Python-driven device.
	// Rack passes the device's buffer plus the port's offset (audio.cpp:41-49), so a port reads count * stride - offset
	// floats from there. A negative offset (from JSON: Rack's fromJson takes any) starts before the buffer and one past
	// its end leaves nothing: Python then gets an empty buffer rather than memory outside it (RP-42).
	static size_t span(int stride, int count, int offset) {
		if (count <= 0 || stride <= 0 || offset < 0)
			return 0;
		size_t total = size_t(count) * size_t(stride);
		return size_t(offset) >= total ? 0 : total - size_t(offset);
	}
	size_t inputSpan(int stride, int count) const { return span(stride, count, inputOffset); }
	size_t outputSpan(int stride, int count) const { return span(stride, count, outputOffset); }
	void processBuffer(const float* input, int inputStride, float* output, int outputStride, int count) override {
		buffers.fetch_add(1, std::memory_order_relaxed);
		frames.fetch_add(static_cast<uint64_t>(count), std::memory_order_relaxed);
		lastFrames.store(count, std::memory_order_relaxed);
		if (io::AudioBridge* b = bridge.load(std::memory_order_acquire)) {
			b->portBuffer(bridgeModule);  // as Core Audio, the master steps Rack's engine
			return;
		}
		if (!pythonDriving || !runtime::interpreterAlive())
			return;
		py::gil_scoped_acquire gil;
		if (auto f = rackpy::bindings::pythonOverride(static_cast<const a::Port*>(this), "process_buffer")) {
			py::object in = floatArray(input, inputSpan(inputStride, count));
			py::object out = floatArray(output, outputSpan(outputStride, count));
			f(in, inputStride, out, outputStride, count);
			copyBack(out, output, outputSpan(outputStride, count));
		}
	}
	void processInput(const float* input, int inputStride, int count) override {
		inputCalls.fetch_add(1, std::memory_order_relaxed);
		if (io::AudioBridge* b = bridge.load(std::memory_order_acquire)) {
			b->portInput(*this, bridgeModule, bridgeModule->engineOwned.load(std::memory_order_acquire), input,
				inputStride, count);
			return;
		}
		if (!pythonDriving || !runtime::interpreterAlive())
			return;
		py::gil_scoped_acquire gil;
		if (auto f = rackpy::bindings::pythonOverride(static_cast<const a::Port*>(this), "process_input"))
			f(floatArray(input, inputSpan(inputStride, count)), inputStride, count);
	}
	void processOutput(float* output, int outputStride, int count) override {
		outputCalls.fetch_add(1, std::memory_order_relaxed);
		if (io::AudioBridge* b = bridge.load(std::memory_order_acquire)) {
			b->portOutput(output, outputStride, count);
			return;
		}
		if (!pythonDriving || !runtime::interpreterAlive())
			return;
		py::gil_scoped_acquire gil;
		if (auto f = rackpy::bindings::pythonOverride(static_cast<const a::Port*>(this), "process_output")) {
			py::object out = floatArray(output, outputSpan(outputStride, count));
			f(out, outputStride, count);
			copyBack(out, output, outputSpan(outputStride, count));
		}
	}
	void queue(StreamEvent event) noexcept {
		std::unique_lock<std::mutex> lock(eventMutex, std::try_to_lock);
		if (!lock.owns_lock() || events.size() >= 256) {
			droppedEvents.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		try {
			events.push_back(event);
		}
		catch (...) {
			droppedEvents.fetch_add(1, std::memory_order_relaxed);
		}
	}
	void queueInitialStart() noexcept {
		if (device)
			queue(StreamEvent::Start);
	}
	void stream(StreamEvent event) {
		if (pythonDriving && runtime::interpreterAlive()) {  // on Python's thread: synchronously
			py::gil_scoped_acquire gil;
			if (auto f = rackpy::bindings::pythonOverride(static_cast<const a::Port*>(this),
					event == StreamEvent::Start ? "on_start_stream" : "on_stop_stream"))
				f();
			return;
		}
		queue(event);
	}
	// Rack's drivers start and stop streams on the main thread (opening and closing them), as Python-driven devices do.
	void onStartStream() override {
		if (io::AudioBridge* b = bridge.load(std::memory_order_acquire))
			b->portStart();
		stream(StreamEvent::Start);
	}
	void onStopStream() override {
		if (io::AudioBridge* b = bridge.load(std::memory_order_acquire))
			b->portStop(bridgeModule);
		stream(StreamEvent::Stop);
	}
};

std::vector<PyAudioPort*> ports;  // main thread
std::vector<UnlistedAudioDevice*> unlistedDevices;  // main thread
bool hooked = false;

/** Prepares the converters each module's audio port will need and frees retired ones (RP-31J). Main thread, UI
step; only Rack's own drivers and Python's configured ones answer (a laboratory driver's port cannot be a module's). */
void maintainBridges() noexcept {
	rack::Context* context = rack::contextGet();
	float engineRate = context && context->engine ? context->engine->getSampleRate() : 0.f;
	for (auto* p : ports) {
		if (!p->bridgeOwner || p->released)
			continue;
		try {
			float deviceRate = p->getSampleRate();
			// Prepare the requested fixed rate separately from the converter
			// still needed by the current rate. Rack's menu offers 11.025–768 kHz.
			float requestedRate = rack::settings::sampleRate;
			if (!(requestedRate >= 11025.f && requestedRate <= 768000.f))
				requestedRate = 0.f;
			p->bridgeOwner->maintain(deviceRate, engineRate, p->getNumInputs(), p->getNumOutputs(), requestedRate);
		}
		catch (const std::exception& e) {
			WARN("RackPy: an audio port could not prepare a resampler: %s", e.what());
		}
	}
}

void releaseRequestedProcessMutexGuards() noexcept;

/** Runs Python ports' on_start_stream and on_stop_stream for their queued events. Main thread, UI step. */
void deliverEvents() noexcept {
	releaseRequestedProcessMutexGuards();
	maintainBridges();
	auto all = ports;
	for (auto* p : all) {
		if (std::find(ports.begin(), ports.end(), p) == ports.end() || p->released)
			continue;
		std::vector<StreamEvent> due;
		{
			std::lock_guard<std::mutex> lock(p->eventMutex);
			due.swap(p->events);
		}
		if (due.empty())
			continue;
		const a::Port* self = p;
		runtime::callPython("audio stream event", [&] {
			for (StreamEvent event : due) {
				if (std::find(ports.begin(), ports.end(), p) == ports.end())
					return;
				if (auto f = rackpy::bindings::pythonOverride(self, event == StreamEvent::Start ? "on_start_stream" : "on_stop_stream"))
					f();
			}
		});
	}
}

PyAudioPort::PyAudioPort() {
	ports.push_back(this);
	// Rack's Port constructor may attach its default device before this Python trampoline exists, so its virtual
	// onStartStream() call resolves to Rack's no-op. Deliver the initial notification after pybind creates the Python
	// peer, just as for later stream changes.
	queueInitialStart();
	if (hooked)
		return;
	hooked = true;
	runtime::onUiStep([] { deliverEvents(); });
	// Rack's audio drivers outlive the context but not RackPy's interpreter: give the devices back while they exist.
	runtime::onContextClosing([] {
		auto all = ports;
		for (auto* p : all)
			p->release();
	});
}

PyAudioPort::~PyAudioPort() {
	release();
	ports.erase(std::remove(ports.begin(), ports.end(), this), ports.end());
}

void PyAudioPort::release() noexcept {
	if (released)
		return;
	released = true;
	try {
		for (auto& d : driverSubscriptions)
			if (auto* driver = a::getDriver(d.driverId))
				driver->unsubscribe(d.deviceId, this);
		for (auto* device : deviceSubscriptions)
			device->unsubscribe(this);
	}
	catch (const std::exception& e) {
		WARN("RackPy: an audio port could not leave a device: %s", e.what());
	}
	driverSubscriptions.clear();
	deviceSubscriptions.clear();
	try {
		setDeviceId(-1);  // a laboratory driver's and device's overrides see a released port and answer natively
	}
	catch (...) {
	}
	// Python overrides of the laboratory's subscribe may have left this port in a device it does not hold.
	for (auto* device : unlistedDevices)
		device->a::Device::unsubscribe(this);
	driver = nullptr;
	driverId = -1;
	detachBridge();  // no device calls it any more; its module keeps the bridge, and hears silence
}

void PyAudioPort::detachBridge() noexcept {
	{
		std::unique_lock<std::mutex> lock;
		if (device) {
			try {
				lock = lockProcess(*device, "detaching a module's audio port");
			}
			catch (const std::exception& e) {
				// Only on the thread inside the device's callbacks, which holds the mutex: the bridge is not running.
				WARN("RackPy: %s", e.what());
			}
		}
		bridge.store(nullptr, std::memory_order_release);
	}
	if (bridgeModule)
		bridgeModule->detachAudio = nullptr;
	bridgeModule = nullptr;
	bridgeOwner.reset();
}

PyAudioPort& checked(a::Port& port);

/** A port whose channel limits may change: not a module's audio port, whose rings have their sizes. */
PyAudioPort& unbridged(a::Port& port) {
	PyAudioPort& p = checked(port);
	if (p.bridgeOwner)
		throw runtime::Ownership("a module's audio port keeps the channels Module.set_audio_port gave it");
	return p;
}

PyAudioPort& checked(a::Port& port) {
	runtime::requireMainThread("using an audio port");
	auto* p = ::rackpy::rttiCast<PyAudioPort*>(&port);
	if (!p)
		throw runtime::Ownership("this audio port is not one Python made");
	if (p->released)
		throw runtime::StaleHandle("Rack's context closed: this audio port gave up its device");
	if (port.context != rack::contextGet())
		throw runtime::WrongContext("this audio port belongs to another Rack context");
	return *p;
}

/** One of Rack's audio drivers, by its ID: drivers live until Rack's shutdown. */
struct AudioDriverView {
	int id;
	a::Driver& get() const {
		runtime::requireMainThread("using an audio driver");
		auto* driver = a::getDriver(id);
		if (!driver)
			throw runtime::StaleHandle("there is no audio driver " + std::to_string(id));
		return *driver;
	}
};

py::object driverObject(int id) {
	return a::getDriver(id) ? py::cast(AudioDriverView{id}) : py::none();
}

/** A device, through the Python port that holds it. */
struct AudioDeviceView {
	py::object owner;
	a::Device* device;
	a::Device& get() const {
		auto& port = checked(owner.cast<a::Port&>());
		if (!device || !port.holds(device))
			throw runtime::StaleHandle("the port no longer holds this audio device");
		return *device;
	}
};

struct AudioProcessMutexView {
	AudioDeviceView device;
};

struct AudioProcessMutexGuardState {
	AudioDeviceView device;
	std::unique_lock<std::mutex> lock;
	a::Device* lockedDevice = nullptr;
	std::thread::id thread = std::this_thread::get_id();
	std::atomic<bool> releaseRequested{false};
};
thread_local std::vector<std::shared_ptr<AudioProcessMutexGuardState>> processMutexGuardStates;
bool processMutexGuardCleanupHooked = false;

void releaseProcessMutexGuard(const std::shared_ptr<AudioProcessMutexGuardState>& state) {
	if (state->thread != std::this_thread::get_id())
		throw runtime::Ownership("a process-mutex guard must be released on the UI thread that acquired it");
	if (state->lock.owns_lock()) {
		forgetProcessMutexGuard(state->lockedDevice);
		state->lock.unlock();
		state->lockedDevice = nullptr;
	}
	auto it = std::find(processMutexGuardStates.begin(), processMutexGuardStates.end(), state);
	if (it != processMutexGuardStates.end())
		processMutexGuardStates.erase(it);
}

void releaseRequestedProcessMutexGuards() noexcept {
	py::gil_scoped_acquire gil;
	for (size_t i = 0; i < processMutexGuardStates.size();) {
		auto state = processMutexGuardStates[i];
		if (state->releaseRequested.load(std::memory_order_acquire)) {
			if (state->lock.owns_lock()) {
				forgetProcessMutexGuard(state->lockedDevice);
				state->lock.unlock();
				state->lockedDevice = nullptr;
			}
			processMutexGuardStates.erase(processMutexGuardStates.begin() + static_cast<std::ptrdiff_t>(i));
		} else {
			i++;
		}
	}
}

void releaseAllProcessMutexGuards() noexcept {
	py::gil_scoped_acquire gil;
	for (const auto& state : processMutexGuardStates) {
		if (state->thread == std::this_thread::get_id() && state->lock.owns_lock()) {
			forgetProcessMutexGuard(state->lockedDevice);
			state->lock.unlock();
			state->lockedDevice = nullptr;
		}
	}
	processMutexGuardStates.clear();
}

void ensureProcessMutexGuardCleanup() {
	if (processMutexGuardCleanupHooked)
		return;
	processMutexGuardCleanupHooked = true;
	runtime::onUiStep([] { releaseRequestedProcessMutexGuards(); });
	runtime::onContextClosing([] {
		releaseAllProcessMutexGuards();
		processMutexGuardCleanupHooked = false;
	});
}

struct AudioProcessMutexGuard {
	std::shared_ptr<AudioProcessMutexGuardState> state;

	AudioProcessMutexGuard(AudioDeviceView view, bool wait) {
		runtime::requireMainThread("audio Device.process_mutex.acquire");
		ensureProcessMutexGuardCleanup();
		state = std::make_shared<AudioProcessMutexGuardState>();
		state->device = std::move(view);
		a::Device& d = state->device.get();
		if (wait)
			state->lock = lockProcess(d, "acquiring a device's processMutex");
		else {
			if (processMutexGuardedHere(d))
				throw runtime::Ownership("a device's processMutex cannot be acquired twice on the same thread");
			if (drivenHere(d, true))
				throw runtime::Ownership("acquiring a device's processMutex is refused inside its own process callbacks");
			state->lock = std::unique_lock<std::mutex>(d.processMutex, std::try_to_lock);
		}
		if (state->lock.owns_lock()) {
			state->lockedDevice = &d;
			processMutexGuardStates.push_back(state);
			try {
				processMutexGuards.push_back(&d);
			} catch (...) {
				processMutexGuardStates.pop_back();
				throw;
			}
		}
	}
	~AudioProcessMutexGuard() {
		if (!state)
			return;
		if (state->thread != std::this_thread::get_id()) {
			// The acquiring UI thread alone may inspect or mutate the unique_lock.
			state->releaseRequested.store(true, std::memory_order_release);
			return;
		}
		if (state->lock.owns_lock())
			releaseProcessMutexGuard(state);
	}
	AudioProcessMutexGuard(const AudioProcessMutexGuard&) = delete;
	AudioProcessMutexGuard& operator=(const AudioProcessMutexGuard&) = delete;
	AudioProcessMutexGuard(AudioProcessMutexGuard&&) = default;
	AudioProcessMutexGuard& operator=(AudioProcessMutexGuard&&) = delete;
	bool ownsLock() const {
		checkThread();
		return state && state->lock.owns_lock();
	}
	void checkThread() const {
		if (state && state->thread != std::this_thread::get_id())
			throw runtime::Ownership("a process-mutex guard must be used on the UI thread that acquired it");
	}
	void release() {
		checkThread();
		if (state)
			releaseProcessMutexGuard(state);
	}
};

py::object deviceObject(py::object owner, a::Device* device) {
	return device ? py::cast(AudioDeviceView{std::move(owner), device}) : py::none();
}

/** A port in a device's subscriber set that Python did not make (a module's): read-only. */
struct AudioPortView {
	std::function<a::Device&()> device;  // resolves the device again, or raises
	const a::Port* port;
	const a::Port& get() const {
		a::Device& d = device();
		auto lock = lockProcess(d, "reading a device's port");
		if (!d.subscribed.count(const_cast<a::Port*>(port)))
			throw runtime::StaleHandle("the port no longer subscribes to this device");
		return *port;
	}
};

/** A device's subscribers: Python's own ports, and PortViews of the others. */
py::list subscriberList(a::Device& d, std::function<a::Device&()> resolve) {
	py::list out;
	auto lock = lockProcess(d, "listing a device's subscribers");  // Rack's: ports do not change while it is held
	for (a::Port* port : d.subscribed) {
		if (::rackpy::rttiCast<PyAudioPort*>(port)) {
			if (livePythonAudioPort(port))  // not one being deleted
				out.append(py::cast(port, py::return_value_policy::reference));
		}
		else
			out.append(py::cast(AudioPortView{resolve, port}));
	}
	return out;
}

PyAudioPort& pythonPort(py::handle value) {
	if (!py::isinstance<a::Port>(value))
		throw py::type_error("expected a rackpy.raw.audio.Port");
	return checked(value.cast<a::Port&>());
}

void requireLiveContext(const char* what) {
	runtime::requireMainThread(what);
	auto token = runtime::binding().current();
	auto* c = rack::contextGet();
	if (!token || c != token->context || !c->engine)
		throw runtime::WrongContext(std::string(what) + " needs Rack's live bound UI context");
}

/** Releases the GIL around a device setter, which may reopen a driver's stream; not for the laboratory's devices,
whose Python overrides then run with the GIL held, so that their errors reach the caller. */
struct ReleaseForDriver {
	std::optional<py::gil_scoped_release> release;
	explicit ReleaseForDriver(a::Device* device) {
		if (!::rackpy::rttiCast<UnlistedAudioDevice*>(device))
			release.emplace();
	}
};

template <class T>
std::vector<T> sorted(const std::set<T>& values) {
	return std::vector<T>(values.begin(), values.end());
}

// --- Drivers and devices Python configures (RP-29C) ---------------------------------------
// Rack calls a driver's and device's virtuals from driver threads and the engine thread (Core Audio
// reads getNumInputs and getSampleRate in processInput), so native code answers them all from values
// Python set. Rack's requests (setSampleRate, setBlockSize) are recorded for Python to take on the
// main thread. Python drives the stream from its own thread.

struct ConfiguredDriver : a::Driver {
	std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
	std::mutex devicesMutex;
	std::string name;
	std::map<int, std::unique_ptr<ConfiguredDevice>> devices;
	int nextId = 0;
	int defaultId = -1;

	~ConfiguredDriver() override { alive->store(false); }
	ConfiguredDevice* find(int id, bool includeRemoved = false) {
		auto it = devices.find(id);
		if (it == devices.end() || (it->second->removed && !includeRemoved))
			return nullptr;
		return it->second.get();
	}
	std::string getName() override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		return name;
	}
	std::vector<int> getDeviceIds() override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		std::vector<int> ids;
		for (auto& [id, device] : devices)
			if (!device->removed)
				ids.push_back(id);
		return ids;
	}
	int getDefaultDeviceId() override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		return find(defaultId) ? defaultId : -1;
	}
	std::string getDeviceName(int id) override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		auto* d = find(id);
		return d ? d->getName() : "";
	}
	int getDeviceNumInputs(int id) override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		auto* d = find(id);
		return d ? d->getNumInputs() : 0;
	}
	int getDeviceNumOutputs(int id) override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		auto* d = find(id);
		return d ? d->getNumOutputs() : 0;
	}
	// devicesMutex is not held while the device waits for its processMutex, which a Python-driven processBuffer
	// holds while a Python port's override may read the driver's devices (RP-42); `driving` keeps the device.
	a::Device* subscribe(int id, a::Port* port) override {
		ConfiguredDevice* d;
		{
			std::lock_guard<std::mutex> lock(devicesMutex);
			d = find(id);
			if (!d)
				return nullptr;  // Rack's port keeps no device
			++d->driving;
		}
		d->subscribe(port);
		release(id, d);
		return d;
	}
	void unsubscribe(int id, a::Port* port) override {
		ConfiguredDevice* d;
		{
			std::lock_guard<std::mutex> lock(devicesMutex);
			d = find(id, true);
			if (!d)
				return;
			++d->driving;
		}
		d->unsubscribe(port);
		release(id, d);
	}
	/** Ends a use of a device; a removed device goes once its last port leaves and no call uses it, as RtAudio's. */
	void release(int id, ConfiguredDevice* d) {
		std::lock_guard<std::mutex> lock(devicesMutex);
		if (--d->driving == 0 && d->removed && d->subscribers.load() == 0)
			devices.erase(id);
	}
};

/** Python's handle on a configured driver: Python's until add_driver, then Rack's until Rack quits. */
struct PythonDriver {
	std::unique_ptr<ConfiguredDriver> owned;
	ConfiguredDriver* driver;
	std::shared_ptr<std::atomic<bool>> alive;
	int registeredId = -1;

	explicit PythonDriver(std::string name) : owned(std::make_unique<ConfiguredDriver>()) {
		driver = owned.get();
		alive = driver->alive;
		driver->name = std::move(name);
	}
	ConfiguredDriver& get() const {
		runtime::requireMainThread("configuring an audio driver");
		if (!alive->load())
			throw runtime::StaleHandle("Rack deleted this audio driver (it quit)");
		return *driver;
	}
};

/** Python's handle on one configured device, by its ID in its driver. */
struct PythonDevice {
	std::shared_ptr<std::atomic<bool>> alive;
	ConfiguredDriver* driver;
	int id;
	ConfiguredDevice& get(bool allowRemoved = false) const {
		if (!alive->load())
			throw runtime::StaleHandle("Rack deleted this audio device's driver (it quit)");
		std::lock_guard<std::mutex> lock(driver->devicesMutex);
		auto* d = driver->find(id, true);
		if (!d)
			throw runtime::StaleHandle("this audio device was removed and its last port left");
		if (d->removed && !allowRemoved)
			throw runtime::StaleHandle("this audio device was removed");
		return *d;
	}
};

/** Keeps a device from deletion while Python runs Rack's Device code on it, from any thread. */
struct DeviceLease {
	ConfiguredDriver* driver;
	int id;
	ConfiguredDevice* device;
	DeviceLease(const PythonDevice& handle, bool allowRemoved) : driver(handle.driver), id(handle.id) {
		if (!handle.alive->load())
			throw runtime::StaleHandle("Rack deleted this audio device's driver (it quit)");
		std::lock_guard<std::mutex> lock(driver->devicesMutex);
		device = driver->find(id, true);
		if (!device)
			throw runtime::StaleHandle("this audio device was removed and its last port left");
		if (device->removed && !allowRemoved)
			throw runtime::StaleHandle("this audio device was removed");
		++device->driving;
	}
	~DeviceLease() {
		std::lock_guard<std::mutex> lock(driver->devicesMutex);
		if (--device->driving == 0 && device->removed && device->subscribers.load() == 0)
			driver->devices.erase(id);
	}
	DeviceLease(const DeviceLease&) = delete;
	DeviceLease& operator=(const DeviceLease&) = delete;
};

/** Python runs a device's Rack code on this thread: Python's ports in it are kept alive meanwhile (an override that
dropped one would delete a port Rack is iterating), and calls that change a device's ports are refused. Made with
the GIL held, before it is released. */
struct DrivingScope {
	bool previous = pythonDriving;
	std::vector<py::object> kept;
	DrivingScope(a::Device& device, bool locked) {
		if (locked && drivenHere(device, true))  // Rack's processBuffer would lock the mutex its caller holds
			throw runtime::Ownership("a device's process_buffer is refused inside its own process_buffer callbacks");
		if (!drivenHere(device, false)) {  // a nested scope's ports cannot change: the outer one keeps them
			auto lock = lockProcess(device, "running a device's callbacks");
			for (a::Port* port : device.subscribed)
				if (::rackpy::rttiCast<PyAudioPort*>(port) && livePythonAudioPort(port))
					kept.push_back(py::cast(port, py::return_value_policy::reference));
		}
		pythonDriving = true;
		drivenDevices.push_back({&device, locked});
	}
	~DrivingScope() {
		drivenDevices.pop_back();
		pythonDriving = previous;
	}
	DrivingScope(const DrivingScope&) = delete;
	DrivingScope& operator=(const DrivingScope&) = delete;
};

const float* floatBuffer(const py::buffer_info& info, size_t needed, const char* what) {
	if (info.itemsize != sizeof(float) || info.format != py::format_descriptor<float>::format())
		throw py::type_error(std::string(what) + " must be a float32 buffer (array('f'), say)");
	runtime::requireContiguous(info, what);
	if (static_cast<size_t>(info.size) < needed)
		throw std::invalid_argument(std::string(what) + " needs at least " + std::to_string(needed) + " floats");
	return static_cast<const float*>(info.ptr);
}

}  // namespace

rack::audio::Port& pythonAudioPort(py::handle value) {
	return pythonPort(value);
}

void ConfiguredDevice::subscribe(a::Port* port) {
	auto lock = lockProcess(*this, "subscribing a port");
	subscribed.insert(port);
	subscribers.store(subscribed.size());
}

void ConfiguredDevice::unsubscribe(a::Port* port) {
	auto lock = lockProcess(*this, "unsubscribing a port");
	subscribed.erase(port);
	subscribers.store(subscribed.size());
}

UnlistedAudioDevice::UnlistedAudioDevice() {
	unlistedDevices.push_back(this);
}

UnlistedAudioDevice::~UnlistedAudioDevice() {
	disconnect();
	unlistedDevices.erase(std::remove(unlistedDevices.begin(), unlistedDevices.end(), this), unlistedDevices.end());
}

void UnlistedAudioDevice::disconnect() noexcept {
	a::Device* self = this;
	for (auto* p : ports) {
		if (p->device == self) {
			p->device = nullptr;
			p->deviceId = -1;
		}
		auto& subs = p->deviceSubscriptions;
		subs.erase(std::remove(subs.begin(), subs.end(), self), subs.end());
	}
	std::lock_guard<std::mutex> lock(processMutex);
	subscribed.clear();
}

UnlistedAudioDriver::~UnlistedAudioDriver() {
	detach();
}

void UnlistedAudioDriver::detach() noexcept {
	for (auto* p : ports) {
		if (p->driver != this)
			continue;
		if (p->device)
			p->device->a::Device::unsubscribe(p);  // Rack's own: no Python while the driver goes
		p->device = nullptr;
		p->deviceId = -1;
		p->driver = nullptr;
		p->driverId = -1;
	}
}

bool livePythonAudioPort(const a::Port* port) {
	auto* p = ::rackpy::rttiCast<const PyAudioPort*>(port);
	return p && !p->released && livePythonObject(static_cast<const void*>(port));
}

py::object audioPortObject(a::Port* port) {
	if (!livePythonAudioPort(port))
		return py::object();
	return py::cast(port, py::return_value_policy::reference);  // the port's own Python object
}

void connectAudioDriver(py::handle port, UnlistedAudioDriver& driver) {
	refuseWhileDriving("connecting a laboratory driver");
	PyAudioPort& p = pythonPort(port);
	if (p.bridgeOwner)
		throw runtime::Ownership("this audio port is a module's audio port: a laboratory driver cannot drive it");
	// Rack's Port::setDriverId, for a driver it found: leave the device, take the driver, then its default device.
	p.setDeviceId(-1);
	p.driver = &driver;
	p.driverId = -1;  // Rack lists no such driver
	int deviceId = driver.getDefaultDeviceId();
	if (deviceId >= 0)
		p.setDeviceId(deviceId);
}

py::list audioSubscribers(py::object owner, a::Device& device) {
	return subscriberList(device, [owner, &device]() -> a::Device& { return device; });
}

namespace {

PyAudioPort* moduleAudioPortOf(const module::Module& m) {
	for (auto* p : ports)
		if (p->bridgeModule == &m && !p->released)
			return p;
	return nullptr;
}

/** The module's side of an attachment, undone when the module goes or gets another port. Main thread. */
void detachModulePort(PyAudioPort* port, module::Module* m) noexcept {
	if (std::find(ports.begin(), ports.end(), port) != ports.end() && port->bridgeModule == m)
		port->detachBridge();
	// A module that is not Rack's any more must not stay its master (Core's ~Audio stops its stream for this).
	try {
		rack::Context* context = rack::contextGet();
		if (context && context->engine && context->engine->getMasterModule() == m)
			context->engine->setMasterModule(nullptr);
	}
	catch (...) {
	}
}

}  // namespace

void attachAudioPort(module::Module& m, py::handle value, int inputs, int outputs) {
	runtime::requireMainThread("setting a module's audio port");
	refuseWhileDriving("setting a module's audio port");
	if (m.laboratory)
		throw runtime::Laboratory("a laboratory module's audio is its Python process(); give an ordinary module the port");
	if (m.engineOwned.load(std::memory_order_acquire) || m.onBench)
		throw runtime::Ownership("a module's audio port is set in its constructor callback, or while Python owns the "
			"module and no laboratory Bench steps it: Rack's engine reads it every frame");
	if (value.is_none()) {
		m.detachAudioPort();
		return;
	}
	PyAudioPort& p = pythonPort(value);
	if (p.bridgeOwner && p.bridgeModule != &m)
		throw runtime::Ownership("this audio port is already another module's audio port");
	if (!p.driverSubscriptions.empty() || !p.deviceSubscriptions.empty())
		throw runtime::Ownership("this audio port subscribes to other devices (Driver.subscribe, Device.subscribe); "
			"a module's audio port has only its own device");
	if (::rackpy::rttiCast<UnlistedAudioDriver*>(p.driver))
		throw runtime::Ownership("a laboratory driver's port cannot be a module's audio port");
	for (const char* name : {"process_input", "process_buffer", "process_output"})
		if (rackpy::bindings::pythonOverride(static_cast<const a::Port*>(&p), name))
			throw py::type_error(std::string("a module's audio port runs natively, so its class must not override ") +
				name + " (it would never be called)");
	if (inputs < 0 || inputs > io::MAX_AUDIO_CHANNELS || outputs < 0 || outputs > io::MAX_AUDIO_CHANNELS ||
		inputs + outputs == 0)
		throw std::invalid_argument("a module's audio port has 0 to " + std::to_string(io::MAX_AUDIO_CHANNELS) +
			" inputs and outputs, and at least one channel");
	auto bridge = std::make_shared<io::AudioBridge>(inputs, outputs);
	m.detachAudioPort();
	if (p.bridgeOwner)
		p.detachBridge();
	// As Core's AudioPort constructor: the port processes the module's channels.
	p.maxInputs = inputs;
	p.maxOutputs = outputs;
	rack::Context* context = rack::contextGet();
	bridge->prepare(p.getSampleRate(), context && context->engine ? context->engine->getSampleRate() : 0.f,
		p.getNumInputs(), p.getNumOutputs());
	m.audio = bridge;
	m.publish();
	p.bridgeOwner = bridge;
	p.bridgeModule = &m;
	{
		std::unique_lock<std::mutex> lock;
		if (p.device)
			lock = lockProcess(*p.device, "setting a module's audio port");
		p.bridge.store(bridge.get(), std::memory_order_release);
	}
	PyAudioPort* port = &p;
	module::Module* self = &m;
	m.detachAudio = [port, self] { detachModulePort(port, self); };
}

void maintainAudioPorts() noexcept {
	maintainBridges();
}

py::object moduleAudioPort(module::Module& m) {
	runtime::requireMainThread("reading a module's audio port");
	PyAudioPort* p = moduleAudioPortOf(m);
	return p ? audioPortObject(p) : py::none();
}

py::object moduleAudioStats(module::Module& m) {
	runtime::requireMainThread("reading a module's audio port");
	if (!m.audio)
		return py::none();
	const io::AudioBridge& b = *m.audio;
	py::dict d;
	d["inputs"] = b.inputs;
	d["outputs"] = b.outputs;
	d["device_inputs"] = b.deviceInputs();
	d["device_outputs"] = b.deviceOutputs();
	d["blocks"] = b.stats.blocks.load();
	d["stepped_frames"] = b.stats.steppedFrames.load();
	d["dropped_frames"] = b.stats.droppedFrames.load();
	d["empty_frames"] = b.stats.emptyFrames.load();
	d["clears"] = b.stats.clears.load();
	d["resampler_misses"] = b.stats.resamplerMisses.load();
	d["frames_to_engine"] = b.toEngineFrames();
	d["frames_to_device"] = b.toDeviceFrames();
	return d;
}

void queueInitialAudioStartForTesting(py::handle value) {
	auto& port = pythonAudioPort(value);
	static_cast<PyAudioPort&>(port).queueInitialStart();
}

void deliverAudioEventsForTesting() noexcept {
	deliverEvents();
}

void bindAudio(py::module_ mod) {
	py::class_<AudioDriverView>(mod, "Driver", py::is_final(), "One of Rack's audio drivers (get_driver), by its ID. Main thread.")
		.def_property_readonly("id", [](const AudioDriverView& d) { return d.id; })
		.def("get_name", [](const AudioDriverView& d) { return d.get().getName(); })
		.def("get_device_ids", [](const AudioDriverView& d) { return d.get().getDeviceIds(); })
		.def("get_default_device_id", [](const AudioDriverView& d) { return d.get().getDefaultDeviceId(); })
		.def("get_device_name", [](const AudioDriverView& d, int id) { return d.get().getDeviceName(id); }, py::arg("device_id"))
		.def("get_device_num_inputs", [](const AudioDriverView& d, int id) { return d.get().getDeviceNumInputs(id); },
			py::arg("device_id"))
		.def("get_device_num_outputs", [](const AudioDriverView& d, int id) { return d.get().getDeviceNumOutputs(id); },
			py::arg("device_id"))
		.def("subscribe", [](const AudioDriverView& d, int deviceId, py::object port) -> py::object {
			refuseWhileDriving("Driver.subscribe");
			auto& p = pythonPort(port);
			if (p.bridgeOwner)
				throw runtime::Ownership("a module's audio port has only its own device");
			a::Device* device = d.get().subscribe(deviceId, &p);
			if (!device)
				return py::none();
			p.driverSubscriptions.push_back({d.id, deviceId, device});
			return deviceObject(port, device);
		}, py::arg("device_id"), py::arg("port"),
			"Rack's: makes the port a subscriber of the device (which may open its stream), apart from the port's own "
			"device; RackPy undoes it when the port dies or the context closes. Port.set_device_id is the usual way.")
		.def("unsubscribe", [](const AudioDriverView& d, int deviceId, py::object port) {
			refuseWhileDriving("Driver.unsubscribe");
			auto& p = pythonPort(port);
			auto it = std::find_if(p.driverSubscriptions.begin(), p.driverSubscriptions.end(),
				[&](const PyAudioPort::Direct& s) { return s.driverId == d.id && s.deviceId == deviceId; });
			if (it == p.driverSubscriptions.end())
				throw runtime::Ownership("the port did not subscribe to this device through the driver");
			d.get().unsubscribe(deviceId, &p);
			p.driverSubscriptions.erase(it);
		}, py::arg("device_id"), py::arg("port"))
		.def("__eq__", [](const AudioDriverView& x, const AudioDriverView& y) { return x.id == y.id; })
		.def("__hash__", [](const AudioDriverView& d) { return py::hash(py::int_(d.id)); })
		.def("__repr__", [](const AudioDriverView& d) { return "<audio.Driver " + std::to_string(d.id) + ">"; });

	py::class_<AudioPortView>(mod, "PortView", py::is_final(),
		"A port Python did not make (a module's), listed by a device's subscribers: read-only, found again at every use.")
		.def_property_readonly("alive", [](const AudioPortView& v) {
			try {
				v.get();
				return true;
			}
			catch (const std::exception&) {
				return false;
			}
		})
		.def_property_readonly("driver_id", [](const AudioPortView& v) { return v.get().driverId; })
		.def_property_readonly("device_id", [](const AudioPortView& v) { return v.get().deviceId; })
		.def_property_readonly("input_offset", [](const AudioPortView& v) { return v.get().inputOffset; })
		.def_property_readonly("output_offset", [](const AudioPortView& v) { return v.get().outputOffset; })
		.def_property_readonly("max_inputs", [](const AudioPortView& v) { return v.get().maxInputs; })
		.def_property_readonly("max_outputs", [](const AudioPortView& v) { return v.get().maxOutputs; });

	py::class_<AudioProcessMutexGuard>(mod, "ProcessMutexGuard", py::is_final(),
		"RAII guard that releases a device's process mutex on the UI thread that acquired it.")
		.def("release", &AudioProcessMutexGuard::release, "Release the mutex; safe to call more than once.")
		.def_property_readonly("owns_lock", &AudioProcessMutexGuard::ownsLock)
		.def("__enter__", [](AudioProcessMutexGuard& guard) -> AudioProcessMutexGuard& {
			guard.checkThread();
			if (!guard.ownsLock())
				throw runtime::Ownership("this process-mutex guard does not own the lock");
			return guard;
		}, py::return_value_policy::reference_internal)
		.def("__exit__", [](AudioProcessMutexGuard& guard, py::object, py::object, py::object) {
			guard.release();
			return false;
		}, py::arg("exc_type"), py::arg("exc_value"), py::arg("traceback"));

	py::class_<AudioProcessMutexView>(mod, "ProcessMutex", py::is_final(),
		"A lifetime-checked view of an audio device's process mutex.")
		.def("acquire", [](const AudioProcessMutexView& view) {
			return AudioProcessMutexGuard(view.device, true);
		}, "Acquire the mutex, releasing the GIL while waiting; use the returned guard as a context manager.")
		.def("try_acquire", [](const AudioProcessMutexView& view) -> py::object {
			AudioProcessMutexGuard guard(view.device, false);
			if (!guard.ownsLock())
				return py::none();
			return py::cast(std::move(guard));
		}, "Try to acquire the mutex; return a context-manager guard or None without waiting.");

	py::class_<AudioDeviceView>(mod, "Device", py::is_final(),
		"An audio device of a driver, through the Python port that holds it. Rack's device methods may raise Rack's "
		"Exception from the driver.")
		.def_property_readonly("alive", [](const AudioDeviceView& v) {
			try {
				v.get();
				return true;
			}
			catch (const std::exception&) {
				return false;
			}
		})
		.def("get_name", [](const AudioDeviceView& v) { return v.get().getName(); })
		.def("get_num_inputs", [](const AudioDeviceView& v) { return v.get().getNumInputs(); })
		.def("get_num_outputs", [](const AudioDeviceView& v) { return v.get().getNumOutputs(); })
		.def("get_sample_rates", [](const AudioDeviceView& v) { return sorted(v.get().getSampleRates()); },
			"Rack's set of selectable rates, sorted.")
		.def("get_sample_rate", [](const AudioDeviceView& v) { return v.get().getSampleRate(); })
		.def("set_sample_rate", [](const AudioDeviceView& v, float rate) {
			a::Device& d = v.get();
			ReleaseForDriver release(&d);  // the driver may reopen its stream
			d.setSampleRate(rate);
		}, py::arg("sample_rate"))
		.def("get_block_sizes", [](const AudioDeviceView& v) { return sorted(v.get().getBlockSizes()); })
		.def("get_block_size", [](const AudioDeviceView& v) { return v.get().getBlockSize(); })
		.def("set_block_size", [](const AudioDeviceView& v, int size) {
			a::Device& d = v.get();
			ReleaseForDriver release(&d);
			d.setBlockSize(size);
		}, py::arg("block_size"))
		.def_property_readonly("process_mutex", [](const AudioDeviceView& v) {
			v.get();
			return AudioProcessMutexView{v};
		}, "Rack's processMutex as a guarded lock view; use only briefly on the UI thread.")
		.def_property_readonly("subscribed", [](py::object self) {
			a::Device& d = self.cast<const AudioDeviceView&>().get();
			return subscriberList(d, [self]() -> a::Device& { return self.cast<const AudioDeviceView&>().get(); });
		}, "Rack's subscribers: Python's own ports, and PortViews of the others (modules' ports).")
		.def("subscribe", [](const AudioDeviceView& v, py::object port) {
			refuseWhileDriving("Device.subscribe");
			a::Device& d = v.get();
			auto& p = pythonPort(port);
			if (p.bridgeOwner)
				throw runtime::Ownership("a module's audio port has only its own device");
			d.subscribe(&p);
			p.deviceSubscriptions.push_back(&d);
		}, py::arg("port"), "Rack's (its driver's subscribe calls it); RackPy undoes it with the port.")
		.def("unsubscribe", [](const AudioDeviceView& v, py::object port) {
			refuseWhileDriving("Device.unsubscribe");
			a::Device& d = v.get();
			auto& p = pythonPort(port);
			auto it = std::find(p.deviceSubscriptions.begin(), p.deviceSubscriptions.end(), &d);
			if (it == p.deviceSubscriptions.end())
				throw runtime::Ownership("the port did not subscribe to this device with Device.subscribe");
			d.unsubscribe(&p);
			p.deviceSubscriptions.erase(it);
		}, py::arg("port"));

	py::class_<a::Port, PyAudioPort, std::unique_ptr<a::Port, MainThreadDelete<a::Port>>>(mod, "Port",
		"Rack's audio port: a handle on one device of one driver, as Core's Audio modules use. Python owns it; main "
		"thread, in Rack's live UI context. Like Rack's, a new port takes Rack's first driver and its default device "
		"(which may open a real stream).")
		.def(py::init([] {
			requireLiveContext("audio.Port()");
			refuseWhileDriving("audio.Port()");  // Rack's constructor subscribes it to a device
			return new PyAudioPort;
		}))
		.def_property("input_offset", [](a::Port& p) { return checked(p).inputOffset; },
			[](a::Port& p, int v) {
				if (v < 0)  // Rack adds it to the device's buffer: a negative one points before it (RP-42)
					throw py::value_error("input_offset is not negative");
				checked(p).inputOffset = v;
			}, "Rack's: the device's first input channel for this port; not negative.")
		.def_property("output_offset", [](a::Port& p) { return checked(p).outputOffset; },
			[](a::Port& p, int v) {
				if (v < 0)
					throw py::value_error("output_offset is not negative");
				checked(p).outputOffset = v;
			}, "Rack's: the device's first output channel for this port; not negative.")
		.def_property("max_inputs", [](a::Port& p) { return checked(p).maxInputs; },
			[](a::Port& p, int v) { unbridged(p).maxInputs = v; },
			"Rack's: the most input channels this port processes. Fixed while the port is a module's audio port.")
		.def_property("max_outputs", [](a::Port& p) { return checked(p).maxOutputs; },
			[](a::Port& p, int v) { unbridged(p).maxOutputs = v; })
		.def_property_readonly("driver_id", [](a::Port& p) { return checked(p).driverId; })
		.def_property_readonly("device_id", [](a::Port& p) { return checked(p).deviceId; })
		.def_property_readonly("driver", [](a::Port& p) { return checked(p).driver ? driverObject(p.driverId) : py::none(); })
		.def_property_readonly("device", [](py::object self) { return deviceObject(self, checked(self.cast<a::Port&>()).device); })
		.def_property_readonly("context", [](a::Port& p) -> py::object {
			checked(p);
			return py::module_::import("rackpy.raw").attr("context_get")();
		}, "Rack's context the port was made in (the live one: other contexts' ports are refused).")
		.def("reset", [](a::Port& p) {
			refuseWhileDriving("Port.reset");
			laboratory::LaboratoryCall call;
			checked(p).reset();
		}, "Rack's: Rack's first driver and its default device.")
		.def("get_driver", [](a::Port& p) { return checked(p).getDriver() ? driverObject(p.getDriverId()) : py::none(); })
		.def("get_driver_id", [](a::Port& p) { return checked(p).getDriverId(); })
		.def("set_driver_id", [](a::Port& p, int id) {
			refuseWhileDriving("Port.set_driver_id");
			laboratory::LaboratoryCall call;  // a laboratory driver's overrides may drop it while Rack still uses it
			checked(p).setDriverId(id);
		}, py::arg("driver_id"),
			"Rack's: the driver (an unknown ID falls back to Rack's first), then its default device.")
		.def("get_driver_name", [](a::Port& p) { return checked(p).getDriverName(); })
		.def("get_device", [](py::object self) { return deviceObject(self, checked(self.cast<a::Port&>()).getDevice()); })
		.def("get_device_ids", [](a::Port& p) { return checked(p).getDeviceIds(); })
		.def("get_device_id", [](a::Port& p) { return checked(p).getDeviceId(); })
		.def("set_device_id", [](a::Port& p, int id) {
			refuseWhileDriving("Port.set_device_id");
			laboratory::LaboratoryCall call;
			checked(p).setDeviceId(id);
		}, py::arg("device_id"),
			"Rack's: subscribes to the driver's device, which may open its stream (-1: none).")
		.def("get_device_num_inputs", [](a::Port& p, int id) { return checked(p).getDeviceNumInputs(id); },
			py::arg("device_id"))
		.def("get_device_num_outputs", [](a::Port& p, int id) { return checked(p).getDeviceNumOutputs(id); },
			py::arg("device_id"))
		.def("get_device_name", [](a::Port& p, int id) { return checked(p).getDeviceName(id); }, py::arg("device_id"))
		.def("get_sample_rates", [](a::Port& p) { return sorted(checked(p).getSampleRates()); }, "Sorted.")
		.def("get_sample_rate", [](a::Port& p) { return checked(p).getSampleRate(); })
		.def("set_sample_rate", [](a::Port& p, float rate) {
			checked(p);
			ReleaseForDriver release(p.device);
			p.setSampleRate(rate);
		}, py::arg("sample_rate"))
		.def("get_block_sizes", [](a::Port& p) { return sorted(checked(p).getBlockSizes()); }, "Sorted.")
		.def("get_block_size", [](a::Port& p) { return checked(p).getBlockSize(); })
		.def("set_block_size", [](a::Port& p, int size) {
			checked(p);
			ReleaseForDriver release(p.device);
			p.setBlockSize(size);
		}, py::arg("block_size"))
		.def("get_num_inputs", [](a::Port& p) { return checked(p).getNumInputs(); },
			"Rack's: the port's active inputs, after input_offset and max_inputs.")
		.def("get_num_outputs", [](a::Port& p) { return checked(p).getNumOutputs(); })
		.def("to_json", [](a::Port& p) {
			laboratory::LaboratoryCall call;
			json_t* data = checked(p).toJson();
			std::unique_ptr<json_t, decltype(&json_decref)> held(data, json_decref);
			return runtime::fromJson(data);
		})
		.def("from_json", [](a::Port& p, py::handle data) {
			refuseWhileDriving("Port.from_json");  // Rack's chooses the driver and device
			checked(p);
			std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(data), json_decref);
			laboratory::LaboratoryCall call;
			p.fromJson(json.get());
		}, py::arg("data"), "Rack's: the driver, the device with the saved name, its rate and block size, and the offsets.")
		.def("process_buffer", [](a::Port& p, py::object, int, py::object, int, int) { checked(p); },
			py::arg("input"), py::arg("input_stride"), py::arg("output"), py::arg("output_stride"), py::arg("frames"),
			"Rack's base: does nothing. On Rack's audio thread Python never runs: a Python override is called only by a "
			"device Python drives (RP-29C); stream_stats counts Rack's calls.")
		.def("process_input", [](a::Port& p, py::object, int, int) { checked(p); },
			py::arg("input"), py::arg("input_stride"), py::arg("frames"), "As process_buffer.")
		.def("process_output", [](a::Port& p, py::object, int, int) { checked(p); },
			py::arg("output"), py::arg("output_stride"), py::arg("frames"), "As process_buffer.")
		.def("on_start_stream", [](a::Port& p) { checked(p); },
			"Rack's base: does nothing. Override it; RackPy calls it on the UI thread after the stream starts (queued).")
		.def("on_stop_stream", [](a::Port& p) { checked(p); }, "As on_start_stream, when the stream stops.")
		.def_property_readonly("stream_stats", [](a::Port& p) {
			auto& s = checked(p);
			py::dict d;
			d["buffers"] = s.buffers.load();
			d["frames"] = s.frames.load();
			d["last_frames"] = s.lastFrames.load();
			d["input_calls"] = s.inputCalls.load();
			d["output_calls"] = s.outputCalls.load();
			d["dropped_events"] = s.droppedEvents.load();
			return d;
		}, "What Rack's audio thread did with this port: processBuffer calls and frames, the last block's frames, "
			"processInput and processOutput calls, and stream events dropped (a full or busy queue).");

	py::class_<PythonDevice>(mod, "PythonDevice", py::is_final(),
		"An audio device of a PythonDriver: Rack's ports subscribe to it, and Python drives its stream. Its values are "
		"native, so Rack reads them on any thread.")
		.def_property_readonly("id", [](const PythonDevice& d) { return d.id; })
		.def_property_readonly("alive", [](const PythonDevice& d) {
			try {
				d.get(true);
				return true;
			}
			catch (const std::exception&) {
				return false;
			}
		})
		.def_property_readonly("removed", [](const PythonDevice& d) { return d.get(true).removed; })
		.def_property("name", [](const PythonDevice& d) { return d.get(true).getName(); },
			[](const PythonDevice& d, std::string name) {
				auto& dev = d.get();
				std::lock_guard<std::mutex> lock(dev.fieldsMutex);
				dev.name = std::move(name);
			}, "Rack's getName: also how a patch finds the device again.")
		.def_property("num_inputs", [](const PythonDevice& d) { return d.get(true).numInputs.load(); },
			[](const PythonDevice& d, int n) {
				if (n < 0)
					throw std::invalid_argument("channel counts are not negative");
				d.get().numInputs.store(n);
			})
		.def_property("num_outputs", [](const PythonDevice& d) { return d.get(true).numOutputs.load(); },
			[](const PythonDevice& d, int n) {
				if (n < 0)
					throw std::invalid_argument("channel counts are not negative");
				d.get().numOutputs.store(n);
			})
		.def_property("sample_rates", [](const PythonDevice& d) {
				auto& dev = d.get(true);
				std::lock_guard<std::mutex> lock(dev.fieldsMutex);
				return sorted(dev.sampleRates);
			}, [](const PythonDevice& d, std::vector<float> rates) {
				auto& dev = d.get();
				std::lock_guard<std::mutex> lock(dev.fieldsMutex);
				dev.sampleRates = std::set<float>(rates.begin(), rates.end());
			})
		.def_property("sample_rate", [](const PythonDevice& d) { return d.get(true).sampleRate.load(); },
			[](const PythonDevice& d, float rate) {
				if (!(rate > 0.f))
					throw std::invalid_argument("the sample rate must be positive");
				d.get().sampleRate.store(rate);
			}, "What Rack's getSampleRate returns. Rack's setSampleRate also sets it, and records a request.")
		.def_property("block_sizes", [](const PythonDevice& d) {
				auto& dev = d.get(true);
				std::lock_guard<std::mutex> lock(dev.fieldsMutex);
				return sorted(dev.blockSizes);
			}, [](const PythonDevice& d, std::vector<int> sizes) {
				auto& dev = d.get();
				std::lock_guard<std::mutex> lock(dev.fieldsMutex);
				dev.blockSizes = std::set<int>(sizes.begin(), sizes.end());
			})
		.def_property("block_size", [](const PythonDevice& d) { return d.get(true).blockSize.load(); },
			[](const PythonDevice& d, int size) {
				if (size < 1)
					throw std::invalid_argument("the block size must be positive");
				d.get().blockSize.store(size);
			})
		.def_property_readonly("subscribed", [](py::object self) {
			auto& dev = self.cast<const PythonDevice&>().get(true);
			return subscriberList(dev, [self]() -> a::Device& { return self.cast<const PythonDevice&>().get(true); });
		}, "Rack's subscribers: Python's own ports, and PortViews of the others (modules' ports).")
		.def("take_requests", [](const PythonDevice& d) {
			auto& dev = d.get(true);
			std::lock_guard<std::mutex> lock(dev.fieldsMutex);
			std::vector<std::pair<std::string, double>> out;
			out.swap(dev.requests);
			return out;
		}, "Rack's calls to setSampleRate and setBlockSize since the last call, oldest first: [('sample_rate', 48000.0), "
			"('block_size', 128.0), ...]. The values are applied already; a real driver would reopen its stream.")
		.def("start_stream", [](const PythonDevice& d) {
			DeviceLease lease(d, false);
			DrivingScope driving(*lease.device, false);
			py::gil_scoped_release release;
			lease.device->onStartStream();
		}, "Rack's Device::onStartStream, as driver code calls it: each subscribed port's on_start_stream (a Python "
			"port's override runs now, on this thread).")
		.def("stop_stream", [](const PythonDevice& d) {
			DeviceLease lease(d, true);
			DrivingScope driving(*lease.device, false);
			py::gil_scoped_release release;
			lease.device->onStopStream();
		})
		.def("process_buffer", [](const PythonDevice& d, py::buffer input, int inputStride, py::buffer output,
			int outputStride, int frames) {
			DeviceLease lease(d, false);
			auto& dev = *lease.device;
			if (frames < 0 || inputStride < 0 || outputStride < 0)
				throw std::invalid_argument("frames and strides are not negative");
			if (inputStride < dev.getNumInputs() || outputStride < dev.getNumOutputs())
				throw std::invalid_argument("each stride must be at least the device's channel count");
			py::buffer_info in = input.request();
			py::buffer_info out = output.request(true);
			const float* inData = floatBuffer(in, size_t(frames) * inputStride, "input");
			float* outData = const_cast<float*>(floatBuffer(out, size_t(frames) * outputStride, "output"));
			if (out.readonly)
				throw py::type_error("the output buffer must be writable");
			auto token = runtime::binding().current();
			if (!token || !token->alive())
				throw runtime::WrongContext("a device streams to Rack's ports only while Rack's context is live");
			DrivingScope driving(dev, true);
			py::gil_scoped_release release;  // Rack's ports run (Core Audio steps the engine); Python ports take it back
			dev.processBuffer(inData, inputStride, outData, outputStride, frames);
		}, py::arg("input"), py::arg("input_stride"), py::arg("output"), py::arg("output_stride"), py::arg("frames"),
			"Rack's Device::processBuffer, as driver code calls it: interleaved float32 frames in and out (output is "
			"overwritten). Each subscribed port's process_input, process_buffer, process_output run on this thread, a "
			"Python port's overrides with copies of its part of the buffers.");

	py::class_<PythonDriver>(mod, "PythonDriver", py::is_final(),
		"An audio driver Python configures: devices with native values that Rack reads on any thread. Give it to "
		"Rack with add_driver; Rack keeps it until it quits.")
		.def(py::init([](std::string name) { return new PythonDriver(std::move(name)); }), py::arg("name"))
		.def_property_readonly("registered_id", [](const PythonDriver& d) { return d.registeredId; },
			"The ID add_driver registered it with, or -1.")
		.def_property("name", [](const PythonDriver& d) { return d.get().getName(); },
			[](const PythonDriver& d, std::string name) {
				auto& driver = d.get();
				std::lock_guard<std::mutex> lock(driver.devicesMutex);
				driver.name = std::move(name);
			})
		.def("add_device", [](const PythonDriver& d, std::string name, int numInputs, int numOutputs,
			std::vector<float> rates, float rate, std::vector<int> sizes, int size) {
			auto& driver = d.get();
			if (numInputs < 0 || numOutputs < 0 || !(rate > 0.f) || size < 1)
				throw std::invalid_argument("channel counts are not negative, and the rate and block size are positive");
			auto device = std::make_unique<ConfiguredDevice>();
			device->name = std::move(name);
			device->numInputs.store(numInputs);
			device->numOutputs.store(numOutputs);
			device->sampleRates = std::set<float>(rates.begin(), rates.end());
			device->sampleRate.store(rate);
			device->blockSizes = std::set<int>(sizes.begin(), sizes.end());
			device->blockSize.store(size);
			std::lock_guard<std::mutex> lock(driver.devicesMutex);
			int id = driver.nextId++;
			driver.devices[id] = std::move(device);
			if (driver.defaultId < 0)
				driver.defaultId = id;
			return PythonDevice{d.alive, d.driver, id};
		}, py::arg("name"), py::arg("num_inputs") = 2, py::arg("num_outputs") = 2,
			py::arg("sample_rates") = std::vector<float>{44100.f, 48000.f}, py::arg("sample_rate") = 48000.f,
			py::arg("block_sizes") = std::vector<int>{64, 128, 256, 512}, py::arg("block_size") = 256,
			"A new device (a hotplug, once Rack has the driver). Its ID is stable; the first is the default.")
		.def("remove_device", [](const PythonDriver& d, const PythonDevice& device) {
			d.get();
			if (device.driver != d.driver)
				throw runtime::Ownership("the device belongs to another driver");
			DeviceLease lease(device, false);
			{
				DrivingScope driving(*lease.device, false);
				py::gil_scoped_release release;
				lease.device->onStopStream();  // the stream ends; ports keep the device until they leave it
			}
			std::lock_guard<std::mutex> lock(d.driver->devicesMutex);
			lease.device->removed = true;  // the lease's end deletes it if no port holds it
		}, py::arg("device"),
			"Unplugs a device: its stream stops, Rack's device lists no longer show it, and it is deleted once its last "
			"port leaves (Rack's ports keep it until then, as with RtAudio).")
		.def_property("default_device_id", [](const PythonDriver& d) { return d.get().getDefaultDeviceId(); },
			[](const PythonDriver& d, int id) {
				auto& driver = d.get();
				std::lock_guard<std::mutex> lock(driver.devicesMutex);
				if (id != -1 && !driver.find(id))
					throw std::invalid_argument("no such device");
				driver.defaultId = id;
			})
		.def("get_device_ids", [](const PythonDriver& d) { return d.get().getDeviceIds(); })
		.def("get_device", [](const PythonDriver& d, int id) -> py::object {
			auto& driver = d.get();
			std::lock_guard<std::mutex> lock(driver.devicesMutex);
			if (!driver.find(id))
				return py::none();
			return py::cast(PythonDevice{d.alive, d.driver, id});
		}, py::arg("device_id"));

	mod.def("add_driver", [](int id, PythonDriver& d) {
		runtime::requireMainThread("adding an audio driver");
		if (id == -1)
			throw std::invalid_argument("Rack reserves driver ID -1");
		if (a::getDriver(id))
			throw runtime::Ownership("Rack already has an audio driver with this ID (it is stored in patches)");
		if (!d.owned)
			throw runtime::Ownership("Rack already has this driver");
		a::addDriver(id, d.owned.release());  // Rack takes pointer ownership, until it quits
		d.registeredId = id;
	}, py::arg("driver_id"), py::arg("driver"),
		"Rack's addDriver for a PythonDriver: Rack owns it from now on, until it quits. The ID is stored in patches and "
		"must be unique.");

	mod.def("get_driver_ids", [] {
		runtime::requireMainThread("listing audio drivers");
		return a::getDriverIds();
	}, "Rack's: the IDs of its audio drivers.");
	mod.def("get_driver", [](int id) -> py::object {
		runtime::requireMainThread("finding an audio driver");
		return driverObject(id);
	}, py::arg("driver_id"), "Rack's: the driver with this ID, or None.");
}

}  // namespace bindings
}  // namespace rackpy
