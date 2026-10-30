// SPDX-License-Identifier: GPL-3.0-or-later
// rack::midi -> rackpy.raw.midi (RP-29A): messages, Rack's MIDI drivers and
// devices as checked views, and Python-owned MIDI ports.
//
// Threads: Rack delivers input messages on the MIDI driver's thread, or for
// its Loopback driver on whichever thread sends (often an engine thread). A
// Python Input's on_message therefore never runs there: the native port
// copies each message into a bounded queue (try-lock: it never blocks that
// thread; a full or contended queue counts a drop) and RackPy calls
// on_message on the UI thread at its next step.
//
// Lifetimes: Rack's drivers live until midi::destroy(), which Rack runs after
// deleting its context but before RackPy finalizes Python. So every Python
// port gives up its devices when the context closes, and its handles are
// stale from then on. Devices belong to their driver (Rack's RtMidi driver
// deletes one when its last port leaves), so a device view goes through the
// port that holds it, at every use.
//
// The callback laboratory's drivers and devices (RP-22C, RP-22F, native/
// laboratory/midi.cpp) are Unlisted ones: Rack never lists them, so only
// Python's ports reach them, and the ports' teardown here leaves them without
// running Python.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <array>
#include <map>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

#include <rack.hpp>

#include "../laboratory/laboratory.hpp"
#include "../module/module.hpp"
#include "../io/midi_queue.hpp"
#include "engine/engine_internal.hpp"
#include "support/overrides.hpp"
#include "midi.hpp"
#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/interpreter.hpp"
#include "rackpy/runtime/json.hpp"
#include "rackpy/runtime/python.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;
namespace m = rack::midi;

namespace {

/** Bounded message queue a driver or engine thread fills without blocking. */
struct MessageQueue {
	static constexpr size_t capacity = 1024;
	std::array<m::Message, capacity> slots;
	size_t head = 0, count = 0;
	std::mutex mutex;
	std::atomic<uint64_t> dropped{0};

	void push(const m::Message& message) noexcept {
		std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
		if (!lock.owns_lock() || count == capacity) {
			dropped.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		try {
			slots[(head + count) % capacity] = message;  // reuses the slot's byte storage when it can
			++count;
		}
		catch (...) {
			dropped.fetch_add(1, std::memory_order_relaxed);
		}
	}
	std::vector<m::Message> take() {
		std::lock_guard<std::mutex> lock(mutex);
		std::vector<m::Message> out;
		out.reserve(count);
		for (; count > 0; --count, head = (head + 1) % capacity)
			out.push_back(slots[head]);
		return out;
	}
};

/** What every Python port shares: its subscriptions outside Port::setDeviceId, and its release at context close. */
struct Tracked {
	bool released = false;
	// Subscriptions Python made through Driver.subscribe_* and InputDevice/OutputDevice.subscribe.
	struct DriverSubscription {
		int driverId;
		int deviceId;
		m::Device* device;
	};
	std::vector<DriverSubscription> driverSubscriptions;
	std::vector<m::Device*> deviceSubscriptions;
	bool holds(const m::Device* device) const {
		for (auto& s : driverSubscriptions)
			if (s.device == device)
				return true;
		return std::find(deviceSubscriptions.begin(), deviceSubscriptions.end(), device) != deviceSubscriptions.end();
	}
	auto findDriverSubscription(int driverId, int deviceId) {
		return std::find_if(driverSubscriptions.begin(), driverSubscriptions.end(),
			[&](const DriverSubscription& s) { return s.driverId == driverId && s.deviceId == deviceId; });
	}
	virtual ~Tracked() = default;
	virtual void release() noexcept = 0;
	virtual m::Port& port() = 0;
};

std::vector<Tracked*> ports;  // main thread
// Synchronous MIDI deliveries in progress on this thread. Touched only where Python may run: thread-local
// storage of a dlopen()ed library may allocate on first use, which Rack's driver threads must not do.
thread_local int deliveries = 0;
std::vector<m::Device*> unlistedDevices;  // main thread: the laboratory's (UnlistedMidiInputDevice, ...)
bool closeHooked = false;

void registerPort(Tracked* p) {
	ports.push_back(p);
	if (closeHooked)
		return;
	closeHooked = true;
	// Rack's drivers outlive the context but not RackPy's interpreter: give the devices back while they exist.
	runtime::onContextClosing([] {
		auto all = ports;
		for (auto* p : all)
			p->release();
	});
}

void unregisterPort(Tracked* p) {
	ports.erase(std::remove(ports.begin(), ports.end(), p), ports.end());
}

template <class Base>
struct TrackedPort : Base, Tracked {
	static constexpr bool isInput = std::is_base_of<m::Input, Base>::value;

	TrackedPort() { registerPort(this); }
	~TrackedPort() override {
		release();
		unregisterPort(this);
	}
	m::Port& port() override { return *this; }

	// Rack's Port virtuals, which a Python subclass may override: Rack calls them from its MIDI menus (UI thread).
	std::vector<int> getDeviceIds() override {
		return overrideOr<std::vector<int>>(static_cast<const Base*>(this), "get_device_ids",
			[this] { return Base::getDeviceIds(); });
	}
	int getDefaultDeviceId() override {
		return overrideOr<int>(static_cast<const Base*>(this), "get_default_device_id",
			[this] { return Base::getDefaultDeviceId(); });
	}
	void setDeviceId(int deviceId) override {
		if (released) {
			Base::setDeviceId(deviceId);
			return;
		}
		// Every subscription change of this port passes here, from Python or from Rack's code (its menus,
		// setDriverId, fromJson, reset); Rack's destructors and release() call the base implementation.
		refuseDuringMidiDelivery("Port.setDeviceId");
		overrideOrVoid(static_cast<const Base*>(this), "set_device_id", [this, deviceId] { Base::setDeviceId(deviceId); },
			deviceId);
	}
	std::string getDeviceName(int deviceId) override {
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_device_name",
			[this, deviceId] { return Base::getDeviceName(deviceId); }, deviceId);
	}
	std::vector<int> getChannels() override {
		return overrideOr<std::vector<int>>(static_cast<const Base*>(this), "get_channels",
			[this] { return Base::getChannels(); });
	}

	void release() noexcept override {
		if (released)
			return;
		released = true;
		try {
			for (auto& s : driverSubscriptions)
				if (auto* driver = m::getDriver(s.driverId)) {
					if constexpr (isInput)
						driver->unsubscribeInput(s.deviceId, this);
					else
						driver->unsubscribeOutput(s.deviceId, this);
				}
			for (auto* device : deviceSubscriptions) {
				if constexpr (isInput)
					static_cast<m::InputDevice*>(device)->unsubscribe(this);
				else
					static_cast<m::OutputDevice*>(device)->unsubscribe(this);
			}
		}
		catch (const std::exception& e) {
			WARN("RackPy: a MIDI port could not leave a device: %s", e.what());
		}
		driverSubscriptions.clear();
		deviceSubscriptions.clear();
		try {
			this->Base::setDeviceId(-1);  // Rack's own: no Python override at shutdown
		}
		catch (...) {
		}
		// Python overrides of a laboratory driver's subscribe may have left this port in a device it does not hold.
		for (auto* device : unlistedDevices) {
			if constexpr (isInput) {
				if (auto* in = dynamic_cast<m::InputDevice*>(device))
					in->unsubscribe(this);
			}
			else if (auto* out = dynamic_cast<m::OutputDevice*>(device))
				out->unsubscribe(this);
		}
		this->driver = nullptr;
		this->driverId = -1;
	}
};

/** A laboratory port's on_message, where Rack calls it: inside Rack's loop over a device's subscribers, so inside a
SynchronousMidiDelivery. With the GIL held (Python called Rack's code) an error reaches that caller; otherwise
callPython refuses Rack's threads (counting the refusal) and contains errors. Returns false where Python may not
run; `answered` says whether an override ran to completion. */
template <class Self>
bool deliverSynchronously(const Self* self, Tracked& port, const m::Message& message, bool& answered) {
	answered = false;
	if (!runtime::interpreterAlive())
		return false;
	auto deliver = [&] {
		// Released, or being deallocated (refcount 0, e.g. while its __dict__ is cleared): no delivery.
		py::handle object = port.released ? py::handle() : livePythonObject(&port.port());
		if (!object)
			return;
		SynchronousMidiDelivery delivery(py::reinterpret_borrow<py::object>(object));
		// Declared after the guard, so the bound method (which may hold the last reference) goes first.
		if (auto f = rackpy::bindings::pythonOverride(self, "on_message")) {
			f(m::Message(message));  // an owned copy: Rack may pass on the caller's own message (a Python object)
			answered = true;
		}
	};
	if (PyGILState_Check()) {
		deliver();
		return true;
	}
	if (!runtime::pythonAllowedHere())
		return runtime::callPython("on_message", deliver);  // refused, and counted
	// Rack's own main-thread delivery (its computer keyboard driver): callPython also drains queued releases
	// after `deliver`, still inside Rack's loop, so the delivery counts until it returns.
	++deliveries;
	struct Leave {
		~Leave() { --deliveries; }
	} leave;
	runtime::callPython("on_message", deliver);
	return true;
}

/** A Python Input: on_message runs on the UI thread, from the queue its driver thread fills. A laboratory input
(rackpy.laboratory.MidiInput) runs it synchronously where Python may run, and queues it only on Rack's threads. */
struct PyInput : TrackedPort<m::Input> {
	MessageQueue queue;
	std::atomic<bool> laboratory{false};  // read on driver threads
	void onMessage(const m::Message& message) override {
		bool answered;
		if (!laboratory.load(std::memory_order_relaxed) || !deliverSynchronously<m::Input>(this, *this, message, answered))
			queue.push(message);
	}
};
struct PyInputQueue : TrackedPort<m::InputQueue> {
	std::atomic<io::MidiQueue*> nativeQueue{nullptr};
	std::vector<std::shared_ptr<io::MidiQueue>> retained;
	module::Module* owner = nullptr;  // main thread only
	int ownerIndex = -1;

	void detachNative() noexcept {
		nativeQueue.store(nullptr, std::memory_order_release);
		if (owner && ownerIndex >= 0)
			owner->detachMidi[ownerIndex] = nullptr;
		owner = nullptr;
		ownerIndex = -1;
	}
	void release() noexcept override {
		TrackedPort<m::InputQueue>::release();
		detachNative();
	}
	~PyInputQueue() override { release(); }
	void onMessage(const m::Message& message) override {
		if (auto* q = nativeQueue.load(std::memory_order_acquire))
			q->push(message);
		else
			m::InputQueue::onMessage(message);
	}
};
using PyOutput = TrackedPort<m::Output>;

/** rackpy.laboratory.MidiInputQueue (RP-22F): Rack's InputQueue whose onMessage a Python subclass overrides. Its
only constructor needs the laboratory. Without an override, or where Python may not run, Rack's InputQueue queues. */
struct LaboratoryInputQueue : TrackedPort<m::InputQueue> {
	void onMessage(const m::Message& message) override {
		bool answered;
		deliverSynchronously(static_cast<const LaboratoryInputQueue*>(this), *this, message, answered);
		if (!answered)
			m::InputQueue::onMessage(message);
	}
};

/** Runs Python Inputs' on_message for the messages their queues hold. Main thread, from RackPy's UI step. */
void deliverMessages() noexcept {
	auto all = ports;
	for (auto* p : all) {
		if (std::find(ports.begin(), ports.end(), p) == ports.end())
			continue;  // a callback deleted it
		auto* input = dynamic_cast<PyInput*>(p);
		if (!input || input->released)
			continue;
		std::vector<m::Message> due = input->queue.take();
		if (due.empty())
			continue;
		const m::Input* self = input;
		runtime::callPython("on_message", [&] {
			for (auto& message : due) {
				if (std::find(ports.begin(), ports.end(), p) == ports.end())
					return;
				auto f = rackpy::bindings::pythonOverride(self, "on_message");
				if (!f)
					return;  // nothing to deliver to
				f(message);
			}
		});
	}
}

bool deliveryHooked = false;
void hookDelivery() {
	if (deliveryHooked)
		return;
	deliveryHooked = true;
	runtime::onUiStep([] { deliverMessages(); });
}

/** A Python port, checked: main thread, not released, made in the live context. */
Tracked& tracked(m::Port& port) {
	runtime::requireMainThread("using a MIDI port");
	auto* t = dynamic_cast<Tracked*>(&port);
	if (!t)
		throw runtime::Ownership("this MIDI port is not one Python made");
	if (t->released)
		throw runtime::StaleHandle("Rack's context closed: this MIDI port gave up its devices");
	if (port.context != rack::contextGet())
		throw runtime::WrongContext("this MIDI port belongs to another Rack context");
	return *t;
}

m::Port& checked(m::Port& port) {
	tracked(port);
	return port;
}

void requireLiveContext(const char* what) {
	runtime::requireMainThread(what);
	auto token = runtime::binding().current();
	auto* c = rack::contextGet();
	if (!token || c != token->context || !c->engine)
		throw runtime::WrongContext(std::string(what) + " needs Rack's live bound UI context (MIDI ports timestamp "
			"messages with its engine)");
}

/** One of Rack's MIDI drivers, by its ID: drivers live until Rack's shutdown. */
struct DriverView {
	int id;
	m::Driver& get() const {
		runtime::requireMainThread("using a MIDI driver");
		auto* driver = m::getDriver(id);
		if (!driver)
			throw runtime::StaleHandle("there is no MIDI driver " + std::to_string(id));
		return *driver;
	}
};

py::object driverObject(int id) {
	return m::getDriver(id) ? py::cast(DriverView{id}) : py::none();
}

/** A device, through the Python port that holds it: Rack's driver may delete it once no port does. */
struct DeviceView {
	py::object owner;  // the Python port
	m::Device* device;
	m::Device& get() const {
		auto& port = owner.cast<m::Port&>();
		auto& t = tracked(port);
		bool held = device && (port.device == device || t.holds(device));
		if (!held || !device)
			throw runtime::StaleHandle("the port no longer holds this MIDI device");
		return *device;
	}
};
struct InputDeviceView : DeviceView {};
struct OutputDeviceView : DeviceView {};

py::object deviceObject(py::object owner, m::Device* device, bool input) {
	if (!device)
		return py::none();
	if (input)
		return py::cast(InputDeviceView{{std::move(owner), device}});
	return py::cast(OutputDeviceView{{std::move(owner), device}});
}

/** A port in a device's subscriber set that Python did not make (a module's): read-only, found again in the set. */
struct PortView {
	std::function<m::Device&()> device;  // resolves the device again, or raises
	const m::Port* port;
	bool input;
	const m::Port& get() const {
		m::Device& d = device();
		bool present = input ? static_cast<m::InputDevice&>(d).subscribed.count(const_cast<m::Input*>(
								  static_cast<const m::Input*>(port))) != 0
							 : static_cast<m::OutputDevice&>(d).subscribed.count(const_cast<m::Output*>(
								  static_cast<const m::Output*>(port))) != 0;
		if (!present)
			throw runtime::StaleHandle("the port no longer subscribes to this device");
		return *port;
	}
};

/** The Python object of a port Python made, or nothing. */
py::object portObject(m::Port* port) {
	if (!dynamic_cast<Tracked*>(port))
		return py::object();
	// pybind11 finds the existing Python object through the bound type it was made as.
	if (auto* queue = dynamic_cast<m::InputQueue*>(port))
		return py::cast(queue, py::return_value_policy::reference);
	if (auto* input = dynamic_cast<m::Input*>(port))
		return py::cast(input, py::return_value_policy::reference);
	return py::cast(dynamic_cast<m::Output*>(port), py::return_value_policy::reference);
}

/** A Python port to subscribe or unsubscribe (`what`): refused during a synchronous delivery. */
template <class T>
T& pythonPort(py::handle value, const char* what) {
	refuseDuringMidiDelivery(what);
	if (!py::isinstance<T>(value))
		throw py::type_error(std::string(what) + " takes a RackPy MIDI port of the right direction");
	auto& port = value.cast<T&>();
	tracked(port);
	return port;
}

template <class Set>
py::list subscribers(std::function<m::Device&()> resolve, const Set& set, bool input) {
	py::list out;
	for (auto* port : set) {
		if (py::object o = portObject(port))
			out.append(o);
		else
			out.append(py::cast(PortView{resolve, port, input}));
	}
	return out;
}

std::vector<uint8_t> messageBytes(py::handle value) {
	std::vector<uint8_t> out;
	for (py::handle item : value) {
		int byte = item.cast<int>();
		if (byte < 0 || byte > 255)
			throw std::invalid_argument("MIDI bytes are 0 to 255");
		out.push_back(static_cast<uint8_t>(byte));
	}
	return out;
}

// --- Drivers and devices Python configures (RP-29C) ---------------------------------------
// Native code answers every Driver and Device call. An output device's sendMessage runs on engine
// threads (modules' process()), so it only queues the message (try-lock, bounded, drops counted) for
// Python to take on the main thread; an input device delivers what Python pushes.

struct ConfiguredInput : m::InputDevice {
	std::mutex nameMutex;
	std::string name;
	bool removed = false;
	std::string getName() override {
		std::lock_guard<std::mutex> lock(nameMutex);
		return name;
	}
};

struct ConfiguredOutput : m::OutputDevice {
	std::mutex nameMutex;
	std::string name;
	bool removed = false;
	MessageQueue sent;
	std::string getName() override {
		std::lock_guard<std::mutex> lock(nameMutex);
		return name;
	}
	void sendMessage(const m::Message& message) override { sent.push(message); }
};

struct ConfiguredMidiDriver : m::Driver {
	std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
	std::mutex devicesMutex;
	std::string name;
	std::map<int, std::unique_ptr<ConfiguredInput>> inputs;
	std::map<int, std::unique_ptr<ConfiguredOutput>> outputs;
	int nextInput = 0, nextOutput = 0, defaultInput = -1, defaultOutput = -1;

	~ConfiguredMidiDriver() override { alive->store(false); }
	template <class Map>
	static auto find(Map& map, int id, bool includeRemoved = false) -> decltype(map.begin()->second.get()) {
		auto it = map.find(id);
		if (it == map.end() || (it->second->removed && !includeRemoved))
			return nullptr;
		return it->second.get();
	}
	template <class Map>
	static std::vector<int> ids(Map& map) {
		std::vector<int> out;
		for (auto& [id, device] : map)
			if (!device->removed)
				out.push_back(id);
		return out;
	}
	std::string getName() override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		return name;
	}
	std::vector<int> getInputDeviceIds() override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		return ids(inputs);
	}
	int getDefaultInputDeviceId() override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		return find(inputs, defaultInput) ? defaultInput : -1;
	}
	std::string getInputDeviceName(int id) override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		auto* d = find(inputs, id);
		return d ? d->getName() : "";
	}
	m::InputDevice* subscribeInput(int id, m::Input* input) override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		auto* d = find(inputs, id);
		if (!d)
			return nullptr;
		d->subscribe(input);
		return d;
	}
	void unsubscribeInput(int id, m::Input* input) override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		auto* d = find(inputs, id, true);
		if (!d)
			return;
		d->unsubscribe(input);
		if (d->removed && d->subscribed.empty())
			inputs.erase(id);
	}
	std::vector<int> getOutputDeviceIds() override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		return ids(outputs);
	}
	int getDefaultOutputDeviceId() override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		return find(outputs, defaultOutput) ? defaultOutput : -1;
	}
	std::string getOutputDeviceName(int id) override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		auto* d = find(outputs, id);
		return d ? d->getName() : "";
	}
	m::OutputDevice* subscribeOutput(int id, m::Output* output) override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		auto* d = find(outputs, id);
		if (!d)
			return nullptr;
		d->subscribe(output);
		return d;
	}
	void unsubscribeOutput(int id, m::Output* output) override {
		std::lock_guard<std::mutex> lock(devicesMutex);
		auto* d = find(outputs, id, true);
		if (!d)
			return;
		d->unsubscribe(output);
		if (d->removed && d->subscribed.empty())
			outputs.erase(id);
	}
};

struct PythonMidiDriver {
	std::unique_ptr<ConfiguredMidiDriver> owned;
	ConfiguredMidiDriver* driver;
	std::shared_ptr<std::atomic<bool>> alive;
	int registeredId = -1;
	explicit PythonMidiDriver(std::string name) : owned(std::make_unique<ConfiguredMidiDriver>()) {
		driver = owned.get();
		alive = driver->alive;
		driver->name = std::move(name);
	}
	ConfiguredMidiDriver& get() const {
		runtime::requireMainThread("configuring a MIDI driver");
		if (!alive->load())
			throw runtime::StaleHandle("Rack deleted this MIDI driver (it quit)");
		return *driver;
	}
};

/** Python's handle on one configured device: main thread (Python's pushes and Rack's subscriptions both are). */
template <class Device, bool Input>
struct PythonMidiDevice {
	std::shared_ptr<std::atomic<bool>> alive;
	ConfiguredMidiDriver* driver;
	int id;
	Device& get(bool allowRemoved = false) const {
		runtime::requireMainThread("using a MIDI device");
		if (!alive->load())
			throw runtime::StaleHandle("Rack deleted this MIDI device's driver (it quit)");
		std::lock_guard<std::mutex> lock(driver->devicesMutex);
		Device* d;
		if constexpr (Input)
			d = ConfiguredMidiDriver::find(driver->inputs, id, true);
		else
			d = ConfiguredMidiDriver::find(driver->outputs, id, true);
		if (!d)
			throw runtime::StaleHandle("this MIDI device was removed and its last port left");
		if (d->removed && !allowRemoved)
			throw runtime::StaleHandle("this MIDI device was removed");
		return *d;
	}
};
using PythonMidiInput = PythonMidiDevice<ConfiguredInput, true>;
using PythonMidiOutput = PythonMidiDevice<ConfiguredOutput, false>;

template <class Handle>
void removeDevice(const PythonMidiDriver& d, const Handle& device) {
	refuseDuringMidiDelivery("removing a MIDI device");
	d.get();
	if (device.driver != d.driver)
		throw runtime::Ownership("the device belongs to another driver");
	auto& dev = device.get();
	std::lock_guard<std::mutex> lock(d.driver->devicesMutex);
	dev.removed = true;
	if (dev.subscribed.empty()) {
		if constexpr (std::is_same<Handle, PythonMidiInput>::value)
			d.driver->inputs.erase(device.id);
		else
			d.driver->outputs.erase(device.id);
	}
}

}  // namespace

rack::midi::Port& pythonMidiPort(py::handle value) {
	if (!py::isinstance<m::Port>(value))
		throw py::type_error("expected a rackpy.raw.midi port (Input, InputQueue or Output)");
	return checked(value.cast<m::Port&>());
}

void attachMidiInput(module::Module& module, int index, py::handle value) {
	runtime::requireMainThread("setting a module's MIDI input");
	refuseDuringMidiDelivery("setting a module's MIDI input");
	if (module.laboratory)
		throw runtime::Laboratory("a laboratory module cannot use a production MIDI input");
	if (module.engineOwned.load(std::memory_order_acquire) || module.onBench)
		throw runtime::Ownership("a module's MIDI inputs can change only while Python owns it and no Bench runs it");
	if (index < 0 || index >= io::MAX_MIDI_INPUTS)
		throw std::invalid_argument("MIDI input index out of range");
	if (value.is_none()) {
		module.detachMidiInput(index);
		return;
	}
	auto* port = dynamic_cast<PyInputQueue*>(&pythonMidiPort(value));
	if (!port)
		throw py::type_error("a module MIDI input must be a rackpy.raw.midi.InputQueue");
	if (port->owner && (port->owner != &module || port->ownerIndex != index))
		throw runtime::Ownership("this MIDI input is already attached to a module");
	if (port->inputDevice || !port->driverSubscriptions.empty() || !port->deviceSubscriptions.empty())
		throw runtime::Ownership("disconnect the MIDI input from all devices before attaching it");
	if (port->size() != 0)
		throw runtime::Ownership("empty the MIDI InputQueue before attaching it to a module");
	// A producer may already have loaded an earlier pointer. Retain old queues
	// until the port is destroyed; bound the number of reattachments.
	if (port->retained.size() >= 32)
		throw runtime::Ownership("this MIDI input has reached its reattachment limit");
	auto queue = std::make_shared<io::MidiQueue>();
	port->retained.push_back(queue);
	module.detachMidiInput(index);
	port->detachNative();
	module.midi[index] = queue;
	module.publish();
	port->owner = &module;
	port->ownerIndex = index;
	module.detachMidi[index] = [port] { port->detachNative(); };
	port->nativeQueue.store(queue.get(), std::memory_order_release);
}

void connectOutputDevice(py::handle output, m::OutputDevice& device) {
	refuseDuringMidiDelivery("connecting a MIDI output device");
	auto* port = dynamic_cast<m::Output*>(&pythonMidiPort(output));
	if (!port)
		throw py::type_error("expected a rackpy.raw.midi.Output");
	Tracked& t = tracked(*port);
	if (t.holds(&device))
		throw runtime::Ownership("the port is already connected to this device");
	port->m::Output::setDeviceId(-1);  // Rack's: leave its driver's device (no Python override)
	device.subscribe(port);
	t.deviceSubscriptions.push_back(&device);  // the port leaves the device when it goes, as for subscribe()
	port->device = port->outputDevice = &device;
}

namespace {

/** The port no longer holds its device: Rack's fields point nowhere. */
void dropDevice(m::Port& port) noexcept {
	port.device = nullptr;
	port.deviceId = -1;
	if (auto* input = dynamic_cast<m::Input*>(&port))
		input->inputDevice = nullptr;
	else
		static_cast<m::Output&>(port).outputDevice = nullptr;
}

/** Every Python port leaves `device`, natively: whatever Python overrides did, none keeps a pointer to it. */
void leaveUnlisted(m::Device* device) noexcept {
	for (auto* t : ports) {
		m::Port& port = t->port();
		if (port.device == device)
			dropDevice(port);
		auto& subs = t->deviceSubscriptions;
		subs.erase(std::remove(subs.begin(), subs.end(), device), subs.end());
		auto& direct = t->driverSubscriptions;
		direct.erase(std::remove_if(direct.begin(), direct.end(),
			[device](const Tracked::DriverSubscription& s) { return s.device == device; }), direct.end());
	}
}

void forgetUnlisted(m::Device* device) noexcept {
	unlistedDevices.erase(std::remove(unlistedDevices.begin(), unlistedDevices.end(), device), unlistedDevices.end());
}

}  // namespace

UnlistedMidiInputDevice::UnlistedMidiInputDevice() {
	unlistedDevices.push_back(this);
}

UnlistedMidiInputDevice::~UnlistedMidiInputDevice() {
	disconnect();
	forgetUnlisted(this);
}

void UnlistedMidiInputDevice::disconnect() noexcept {
	leaveUnlisted(this);
	subscribed.clear();
}

UnlistedMidiOutputDevice::UnlistedMidiOutputDevice() {
	unlistedDevices.push_back(this);
}

UnlistedMidiOutputDevice::~UnlistedMidiOutputDevice() {
	disconnect();
	forgetUnlisted(this);
}

void UnlistedMidiOutputDevice::disconnect() noexcept {
	leaveUnlisted(this);
	subscribed.clear();
}

UnlistedMidiDriver::~UnlistedMidiDriver() {
	detach();
}

void UnlistedMidiDriver::detach() noexcept {
	for (auto* t : ports) {
		m::Port& port = t->port();
		if (port.driver != this)
			continue;
		// Rack's own unsubscribe: no Python while the driver goes.
		if (auto* input = dynamic_cast<m::Input*>(&port)) {
			if (input->inputDevice)
				input->inputDevice->unsubscribe(input);
		}
		else if (auto* output = static_cast<m::Output*>(&port); output->outputDevice)
			output->outputDevice->unsubscribe(output);
		dropDevice(port);
		port.driver = nullptr;
		port.driverId = -1;
	}
}

SynchronousMidiDelivery::SynchronousMidiDelivery(py::object port) {
	// Rack's loop iterates a device's set, and holds the delivered port as its current element: neither that
	// port nor any laboratory driver or device may be destroyed until the loop returns.
	laboratory::keepLaboratoryObjectsAlive(std::move(port));
	++deliveries;
}

SynchronousMidiDelivery::~SynchronousMidiDelivery() {
	--deliveries;  // still inside Rack's loop: what was kept alive is released later
}

void refuseDuringMidiDelivery(const char* what) {
	if (deliveries > 0)
		throw runtime::Laboratory(std::string(what) + " is refused during a synchronous MIDI delivery: Rack is "
			"iterating a MIDI device's subscribers while on_message runs, so no subscription may change until the "
			"call that delivered the message returns");
}

bool midiDeliveryInProgress() {
	return deliveries > 0;
}

bool livePythonMidiPort(const m::Port* port) {
	auto* t = dynamic_cast<const Tracked*>(port);
	return t && !t->released && livePythonObject(static_cast<const void*>(port));
}

py::object midiPortObject(m::Port* port) {
	if (!livePythonMidiPort(port))
		return py::object();
	return portObject(port);  // alive (livePythonMidiPort): this reference resurrects nothing
}

void connectMidiDriver(py::handle port, UnlistedMidiDriver& driver) {
	refuseDuringMidiDelivery("connecting a MIDI driver");
	m::Port& p = pythonMidiPort(port);
	// Rack's Port::setDriverId, for a driver it found, through the port's virtuals (a Python port's overrides run).
	p.setDeviceId(-1);
	p.driver = &driver;
	p.driverId = -1;  // Rack lists no such driver
	int deviceId = p.getDefaultDeviceId();
	if (deviceId >= 0)
		p.setDeviceId(deviceId);
}

py::list midiSubscribers(py::object owner, m::InputDevice& device) {
	return subscribers([owner, &device]() -> m::Device& { return device; }, device.subscribed, true);
}

py::list midiSubscribers(py::object owner, m::OutputDevice& device) {
	return subscribers([owner, &device]() -> m::Device& { return device; }, device.subscribed, false);
}

void markLaboratoryInput(py::handle input) {
	auto* port = py::isinstance<m::Input>(input) ? dynamic_cast<PyInput*>(&input.cast<m::Input&>()) : nullptr;
	if (!port)
		throw py::type_error("only a rackpy.raw.midi.Input (not an InputQueue) can be a laboratory input");
	tracked(*port);
	port->laboratory.store(true);
}

bool isLaboratoryInput(py::handle input) {
	auto* port = py::isinstance<m::Input>(input) ? dynamic_cast<PyInput*>(&input.cast<m::Input&>()) : nullptr;
	return port && port->laboratory.load();
}

/** Port has pure virtual methods: Python ports' super path uses the concrete
Input/Output implementation. Native ports still dispatch through their vtable. */
template<class Base, class Virtual>
auto portImplementation(m::Port& p, Base base, Virtual virtualCall) {
	checked(p);
	if (dynamic_cast<Tracked*>(&p)) {
		if (auto* input = dynamic_cast<m::Input*>(&p)) return base(*input);
		return base(static_cast<m::Output&>(p));
	}
	return virtualCall(p);
}

void bindMidi(py::module_ mod) {
	py::class_<m::Message>(mod, "Message",
		"Rack's MIDI message: its bytes (3 zeros at first) and the engine frame it belongs to (-1: now). The "
		"accessors are Rack's: setters mask to 7 bits and ignore missing bytes.")
		.def(py::init<>())
		.def_property("bytes", [](const m::Message& msg) { return std::vector<int>(msg.bytes.begin(), msg.bytes.end()); },
			[](m::Message& msg, py::handle value) { msg.bytes = messageBytes(value); }, "A copy, as a list of ints.")
		.def_readwrite("frame", &m::Message::frame)
		.def("get_size", &m::Message::getSize)
		.def("set_size", [](m::Message& msg, int size) {
			if (size < 0)
				throw std::invalid_argument("a message's size is not negative");
			msg.setSize(size);
		}, py::arg("size"))
		.def("get_channel", &m::Message::getChannel)
		.def("set_channel", &m::Message::setChannel, py::arg("channel"))
		.def("get_status", &m::Message::getStatus)
		.def("set_status", &m::Message::setStatus, py::arg("status"))
		.def("get_note", &m::Message::getNote)
		.def("set_note", &m::Message::setNote, py::arg("note"))
		.def("get_value", &m::Message::getValue)
		.def("set_value", &m::Message::setValue, py::arg("value"))
		.def("to_string", &m::Message::toString)
		.def("get_frame", &m::Message::getFrame)
		.def("set_frame", &m::Message::setFrame, py::arg("frame"))
		.def("__eq__", [](const m::Message& a, const m::Message& b) { return a.bytes == b.bytes && a.frame == b.frame; })
		.def("__repr__", [](const m::Message& msg) {
			return "<Message " + msg.toString() + " frame=" + std::to_string(msg.frame) + ">";
		});

	py::class_<DriverView>(mod, "Driver", py::is_final(),
		"One of Rack's MIDI drivers (get_driver), by its ID. Main thread.")
		.def_property_readonly("id", [](const DriverView& d) { return d.id; })
		.def("get_name", [](const DriverView& d) { return d.get().getName(); })
		.def("get_input_device_ids", [](const DriverView& d) { return d.get().getInputDeviceIds(); })
		.def("get_default_input_device_id", [](const DriverView& d) { return d.get().getDefaultInputDeviceId(); })
		.def("get_input_device_name", [](const DriverView& d, int id) { return d.get().getInputDeviceName(id); },
			py::arg("device_id"))
		.def("get_output_device_ids", [](const DriverView& d) { return d.get().getOutputDeviceIds(); })
		.def("get_default_output_device_id", [](const DriverView& d) { return d.get().getDefaultOutputDeviceId(); })
		.def("get_output_device_name", [](const DriverView& d, int id) { return d.get().getOutputDeviceName(id); },
			py::arg("device_id"))
		.def("subscribe_input", [](const DriverView& d, int deviceId, py::object input) -> py::object {
			auto& port = pythonPort<m::Input>(input, "subscribe_input");
			auto& t = tracked(port);
			auto* device = d.get().subscribeInput(deviceId, &port);
			if (!device)
				return py::none();
			t.driverSubscriptions.push_back({d.id, deviceId, device});
			return deviceObject(input, device, true);
		}, py::arg("device_id"), py::arg("input"),
			"Rack's: makes the port a subscriber of the device, apart from the port's own device; RackPy undoes it "
			"when the port dies or the context closes. Port.set_device_id is the usual way.")
		.def("unsubscribe_input", [](const DriverView& d, int deviceId, py::object input) {
			auto& port = pythonPort<m::Input>(input, "unsubscribe_input");
			auto& t = tracked(port);
			auto it = t.findDriverSubscription(d.id, deviceId);
			if (it == t.driverSubscriptions.end())
				throw runtime::Ownership("the port did not subscribe to this device through the driver");
			d.get().unsubscribeInput(deviceId, &port);
			t.driverSubscriptions.erase(it);
		}, py::arg("device_id"), py::arg("input"))
		.def("subscribe_output", [](const DriverView& d, int deviceId, py::object output) -> py::object {
			auto& port = pythonPort<m::Output>(output, "subscribe_output");
			auto& t = tracked(port);
			auto* device = d.get().subscribeOutput(deviceId, &port);
			if (!device)
				return py::none();
			t.driverSubscriptions.push_back({d.id, deviceId, device});
			return deviceObject(output, device, false);
		}, py::arg("device_id"), py::arg("output"))
		.def("unsubscribe_output", [](const DriverView& d, int deviceId, py::object output) {
			auto& port = pythonPort<m::Output>(output, "unsubscribe_output");
			auto& t = tracked(port);
			auto it = t.findDriverSubscription(d.id, deviceId);
			if (it == t.driverSubscriptions.end())
				throw runtime::Ownership("the port did not subscribe to this device through the driver");
			d.get().unsubscribeOutput(deviceId, &port);
			t.driverSubscriptions.erase(it);
		}, py::arg("device_id"), py::arg("output"))
		.def("__eq__", [](const DriverView& a, const DriverView& b) { return a.id == b.id; })
		.def("__hash__", [](const DriverView& d) { return py::hash(py::int_(d.id)); })
		.def("__repr__", [](const DriverView& d) { return "<midi.Driver " + std::to_string(d.id) + ">"; });

	py::class_<DeviceView>(mod, "Device", "A MIDI device of a driver, through the Python port that holds it.")
		.def_property_readonly("alive", [](const DeviceView& v) {
			try {
				v.get();
				return true;
			}
			catch (const std::exception&) {
				return false;
			}
		})
		.def("get_name", [](const DeviceView& v) { return v.get().getName(); });
	py::class_<InputDeviceView, DeviceView>(mod, "InputDevice")
		.def_property_readonly("subscribed", [](py::object self) {
			auto& v = self.cast<const InputDeviceView&>();
			return subscribers([self]() -> m::Device& { return self.cast<const InputDeviceView&>().get(); },
				static_cast<m::InputDevice&>(v.get()).subscribed, true);
		}, "Rack's subscribers: Python's own ports, and PortViews of the others (modules' ports).")
		.def("subscribe", [](const InputDeviceView& v, py::object input) {
			auto& device = static_cast<m::InputDevice&>(v.get());
			auto& port = pythonPort<m::Input>(input, "InputDevice.subscribe");
			device.subscribe(&port);
			tracked(port).deviceSubscriptions.push_back(&device);
		}, py::arg("input"), "Rack's (not meant for plugins: Driver.subscribe_input is); RackPy undoes it with the port.")
		.def("unsubscribe", [](const InputDeviceView& v, py::object input) {
			auto& device = static_cast<m::InputDevice&>(v.get());
			auto& port = pythonPort<m::Input>(input, "InputDevice.unsubscribe");
			auto& subs = tracked(port).deviceSubscriptions;
			auto it = std::find(subs.begin(), subs.end(), static_cast<m::Device*>(&device));
			if (it == subs.end())
				throw runtime::Ownership("the port did not subscribe to this device with InputDevice.subscribe");
			device.unsubscribe(&port);
			subs.erase(it);
		}, py::arg("input"))
		.def("on_message", [](const InputDeviceView& v, const m::Message& message) {
			requireLiveContext("InputDevice.on_message");
			laboratory::LaboratoryCall call;
			static_cast<m::InputDevice&>(v.get()).onMessage(message);
		}, py::arg("message"),
			"Rack's: delivers the message to the device's subscribers, as the driver does when one arrives (an unset "
			"frame is set to Rack's engine timing).");
	py::class_<OutputDeviceView, DeviceView>(mod, "OutputDevice")
		.def_property_readonly("subscribed", [](py::object self) {
			auto& v = self.cast<const OutputDeviceView&>();
			return subscribers([self]() -> m::Device& { return self.cast<const OutputDeviceView&>().get(); },
				static_cast<m::OutputDevice&>(v.get()).subscribed, false);
		}, "Rack's subscribers: Python's own ports, and PortViews of the others (modules' ports).")
		.def("subscribe", [](const OutputDeviceView& v, py::object output) {
			auto& device = static_cast<m::OutputDevice&>(v.get());
			auto& port = pythonPort<m::Output>(output, "OutputDevice.subscribe");
			device.subscribe(&port);
			tracked(port).deviceSubscriptions.push_back(&device);
		}, py::arg("output"), "Rack's (not meant for plugins: Driver.subscribe_output is); RackPy undoes it with the port.")
		.def("unsubscribe", [](const OutputDeviceView& v, py::object output) {
			auto& device = static_cast<m::OutputDevice&>(v.get());
			auto& port = pythonPort<m::Output>(output, "OutputDevice.unsubscribe");
			auto& subs = tracked(port).deviceSubscriptions;
			auto it = std::find(subs.begin(), subs.end(), static_cast<m::Device*>(&device));
			if (it == subs.end())
				throw runtime::Ownership("the port did not subscribe to this device with OutputDevice.subscribe");
			device.unsubscribe(&port);
			subs.erase(it);
		}, py::arg("output"))
		.def("send_message", [](const OutputDeviceView& v, const m::Message& message) {
			requireLiveContext("OutputDevice.send_message");
			laboratory::LaboratoryCall call;  // Rack's Loopback delivers it now
			static_cast<m::OutputDevice&>(v.get()).sendMessage(message);
		}, py::arg("message"), "Rack's: sends the message to the device as it is (no channel is applied).");

	py::class_<PortView>(mod, "PortView", py::is_final(),
		"A port Python did not make (a module's), listed by a device's subscribers: read-only, found again at every use.")
		.def_property_readonly("alive", [](const PortView& v) {
			try {
				v.get();
				return true;
			}
			catch (const std::exception&) {
				return false;
			}
		})
		.def_property_readonly("driver_id", [](const PortView& v) { return v.get().driverId; })
		.def_property_readonly("device_id", [](const PortView& v) { return v.get().deviceId; })
		.def_property_readonly("channel", [](const PortView& v) { return v.get().channel; });

	py::class_<m::Port, std::unique_ptr<m::Port, MainThreadDelete<m::Port>>> port(mod, "Port",
		"Rack's MIDI port: a handle on one device of one driver. Make an Input, InputQueue or Output; main thread, in "
		"Rack's live UI context.");
	port.def_property("channel", [](m::Port& p) { return checked(p).channel; },
			[](m::Port& p, int channel) { checked(p).channel = channel; },
			"Rack's: an input's channel filter, or the channel an output sets (-1: all, or the messages' own).")
		.def_property_readonly("driver_id", [](m::Port& p) { return checked(p).driverId; })
		.def_property_readonly("device_id", [](m::Port& p) { return checked(p).deviceId; })
		.def_property_readonly("driver", [](m::Port& p) { return checked(p).driver ? driverObject(p.driverId) : py::none(); })
		.def_property_readonly("device", [](py::object self) {
			auto& p = checked(self.cast<m::Port&>());
			return deviceObject(self, p.device, dynamic_cast<m::Input*>(&p) != nullptr);
		})
		.def_property_readonly("context", [](m::Port& p) -> py::object {
			checked(p);
			return py::module_::import("rackpy.raw").attr("context_get")();
		}, "Rack's context the port was made in (the live one: other contexts' ports are refused).")
		.def("get_driver", [](m::Port& p) { return checked(p).getDriver() ? driverObject(p.getDriverId()) : py::none(); })
		.def("get_driver_id", [](m::Port& p) { return checked(p).getDriverId(); })
		.def("set_driver_id", [](m::Port& p, int id) {
			refuseDuringMidiDelivery("Port.set_driver_id");
			laboratory::LaboratoryCall call;  // a laboratory driver's overrides may drop it while Rack still uses it
			checked(p).setDriverId(id);
		}, py::arg("driver_id"),
			"Rack's: the driver (an unknown ID falls back to Rack's first), then its default device.")
		.def("get_device", [](py::object self) {
			auto& p = checked(self.cast<m::Port&>());
			return deviceObject(self, p.getDevice(), dynamic_cast<m::Input*>(&p) != nullptr);
		})
		.def("get_device_id", [](m::Port& p) { return checked(p).getDeviceId(); })
		.def("set_device_id", [](m::Port& p, int id) {
			refuseDuringMidiDelivery("Port.set_device_id");
			laboratory::LaboratoryCall call;
			return portImplementation(p,
				[id](auto& value) { using T = std::decay_t<decltype(value)>; return value.T::setDeviceId(id); },
				[id](m::Port& value) { return value.setDeviceId(id); });
		}, py::arg("device_id"),
			"Rack's: subscribes to the driver's device (-1: none).")
		.def("get_device_ids", [](m::Port& p) {
			return portImplementation(p,
				[](auto& value) { using T = std::decay_t<decltype(value)>; return value.T::getDeviceIds(); },
				[](m::Port& value) { return value.getDeviceIds(); });
		})
		.def("get_default_device_id", [](m::Port& p) {
			return portImplementation(p,
				[](auto& value) { using T = std::decay_t<decltype(value)>; return value.T::getDefaultDeviceId(); },
				[](m::Port& value) { return value.getDefaultDeviceId(); });
		})
		.def("get_device_name", [](m::Port& p, int id) {
			return portImplementation(p,
				[id](auto& value) { using T = std::decay_t<decltype(value)>; return value.T::getDeviceName(id); },
				[id](m::Port& value) { return value.getDeviceName(id); });
		}, py::arg("device_id"))
		.def("get_channels", [](m::Port& p) {
			return portImplementation(p,
				[](auto& value) { using T = std::decay_t<decltype(value)>; return value.T::getChannels(); },
				[](m::Port& value) { return value.getChannels(); });
		})
		.def("get_channel", [](m::Port& p) { return checked(p).getChannel(); })
		.def("set_channel", [](m::Port& p, int channel) { checked(p).setChannel(channel); }, py::arg("channel"))
		.def("get_channel_name", [](m::Port& p, int channel) { return checked(p).getChannelName(channel); },
			py::arg("channel"))
		.def("to_json", [](m::Port& p) {
			laboratory::LaboratoryCall call;
			json_t* data = checked(p).toJson();
			std::unique_ptr<json_t, decltype(&json_decref)> held(data, json_decref);
			return runtime::fromJson(data);
		})
		.def("from_json", [](m::Port& p, py::handle data) {
			refuseDuringMidiDelivery("Port.from_json");
			laboratory::LaboratoryCall call;
			checked(p);
			std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(data), json_decref);
			p.fromJson(json.get());
		}, py::arg("data"), "Rack's: the driver, then the device with the saved name, then the channel.");

	py::class_<m::Input, m::Port, PyInput, std::unique_ptr<m::Input, MainThreadDelete<m::Input>>>(mod, "Input",
		"Rack's MIDI input port. Subclass it and override on_message: RackPy calls it on the UI thread with each "
		"message the driver delivered (queued; up to 1024 wait, more are dropped and counted).")
		.def(py::init([] {
			requireLiveContext("midi.Input()");
			refuseDuringMidiDelivery("midi.Input()");  // Rack's constructor subscribes it
			hookDelivery();
			return new PyInput;
		}))
		.def_property_readonly("input_device", [](py::object self) {
			auto& p = static_cast<m::Input&>(checked(self.cast<m::Port&>()));
			return deviceObject(self, p.inputDevice, true);
		})
		.def("reset", [](m::Input& p) {
			refuseDuringMidiDelivery("Input.reset");
			laboratory::LaboratoryCall call;
			checked(p);
			p.reset();
		}, "Rack's: Rack's first driver and its default device, all channels.")
		.def("on_message", [](m::Input& p, const m::Message&) { checked(p); },
			py::arg("message"), "Rack's base: does nothing. Override it; RackPy calls it on the UI thread (queued).")
		.def_property_readonly("dropped", [](m::Input& p) -> uint64_t {
			checked(p);
			auto* py = dynamic_cast<PyInput*>(&p);
			return py ? py->queue.dropped.load() : 0;
		}, "Messages that found the queue full or busy, and were dropped.");

	py::class_<m::InputQueue, m::Input, std::unique_ptr<m::InputQueue, MainThreadDelete<m::InputQueue>>>(mod, "InputQueue",
		py::is_final(),
		"Rack's input port that keeps messages (up to 8192) until their frame comes: try_pop them on the main thread.")
		.def(py::init([] {
			requireLiveContext("midi.InputQueue()");
			refuseDuringMidiDelivery("midi.InputQueue()");
			return static_cast<m::InputQueue*>(new PyInputQueue);
		}))
		.def("on_message", [](m::InputQueue& q, const m::Message& message) {
			checked(q);
			q.onMessage(message);
		}, py::arg("message"), "Rack's: queues the message, as the driver does.")
		.def("try_pop", [](m::InputQueue& q, int64_t maxFrame) -> py::object {
			checked(q);
			if (auto* p = dynamic_cast<PyInputQueue*>(&q); p && p->owner)
				throw runtime::Ownership("a module's MIDI input is consumed by its native kernel");
			m::Message message;
			if (!q.tryPop(&message, maxFrame))
				return py::none();
			return py::cast(message);
		}, py::arg("max_frame"), "Rack's: the next message if its frame is max_frame or earlier, else None.")
		.def("size", [](m::InputQueue& q) {
			checked(q);
			if (auto* p = dynamic_cast<PyInputQueue*>(&q); p && p->owner)
				throw runtime::Ownership("a module's MIDI input is consumed by its native kernel");
			return q.size();
		});

	py::class_<m::Output, m::Port, PyOutput, std::unique_ptr<m::Output, MainThreadDelete<m::Output>>>(mod, "Output",
		"Rack's MIDI output port. A subclass may override Rack's port virtuals (get_device_ids, get_channels, ...), "
		"which Rack's menus call on the UI thread.")
		.def(py::init([] {
			requireLiveContext("midi.Output()");
			refuseDuringMidiDelivery("midi.Output()");
			return new PyOutput;
		}))
		.def_property_readonly("output_device", [](py::object self) {
			auto& p = static_cast<m::Output&>(checked(self.cast<m::Port&>()));
			return deviceObject(self, p.outputDevice, false);
		})
		.def("reset", [](m::Output& p) {
			refuseDuringMidiDelivery("Output.reset");
			laboratory::LaboratoryCall call;
			checked(p);
			p.reset();
		}, "Rack's: Rack's first driver and its default device, channel 1.")
		.def("send_message", [](m::Output& p, const m::Message& message) {
			checked(p);
			requireLiveContext("Output.send_message");
			laboratory::LaboratoryCall call;  // Rack's Loopback delivers it now
			p.sendMessage(message);
		}, py::arg("message"), "Rack's: sends to the port's device, with the port's channel unless it is -1.");

	py::class_<PythonMidiInput>(mod, "PythonInputDevice", py::is_final(),
		"An input device of a PythonMidiDriver: Rack's input ports subscribe to it, and Python pushes messages into it.")
		.def_property_readonly("id", [](const PythonMidiInput& d) { return d.id; })
		.def_property_readonly("alive", [](const PythonMidiInput& d) {
			try {
				d.get(true);
				return true;
			}
			catch (const std::exception&) {
				return false;
			}
		})
		.def_property_readonly("removed", [](const PythonMidiInput& d) { return d.get(true).removed; })
		.def_property("name", [](const PythonMidiInput& d) { return d.get(true).getName(); },
			[](const PythonMidiInput& d, std::string name) {
				auto& dev = d.get();
				std::lock_guard<std::mutex> lock(dev.nameMutex);
				dev.name = std::move(name);
			})
		.def_property_readonly("subscribed", [](py::object self) {
			auto& d = self.cast<const PythonMidiInput&>();
			return subscribers([self]() -> m::Device& { return self.cast<const PythonMidiInput&>().get(true); },
				d.get(true).subscribed, true);
		}, "Rack's subscribers: Python's own ports, and PortViews of the others (modules' ports).")
		.def("push_message", [](const PythonMidiInput& d, const m::Message& message) {
			requireLiveContext("push_message");
			laboratory::LaboratoryCall call;
			d.get().onMessage(message);
		}, py::arg("message"),
			"Rack's InputDevice::onMessage, as driver code calls it when a message arrives: each subscribed input gets "
			"it (channel-filtered; an unset frame is set from Rack's engine timing).");

	py::class_<PythonMidiOutput>(mod, "PythonOutputDevice", py::is_final(),
		"An output device of a PythonMidiDriver: Rack's output ports subscribe to it, and what they send is queued "
		"for Python (sendMessage runs on engine threads).")
		.def_property_readonly("id", [](const PythonMidiOutput& d) { return d.id; })
		.def_property_readonly("alive", [](const PythonMidiOutput& d) {
			try {
				d.get(true);
				return true;
			}
			catch (const std::exception&) {
				return false;
			}
		})
		.def_property_readonly("removed", [](const PythonMidiOutput& d) { return d.get(true).removed; })
		.def_property("name", [](const PythonMidiOutput& d) { return d.get(true).getName(); },
			[](const PythonMidiOutput& d, std::string name) {
				auto& dev = d.get();
				std::lock_guard<std::mutex> lock(dev.nameMutex);
				dev.name = std::move(name);
			})
		.def_property_readonly("subscribed", [](py::object self) {
			auto& d = self.cast<const PythonMidiOutput&>();
			return subscribers([self]() -> m::Device& { return self.cast<const PythonMidiOutput&>().get(true); },
				d.get(true).subscribed, false);
		}, "Rack's subscribers: Python's own ports, and PortViews of the others (modules' ports).")
		.def("take_sent", [](const PythonMidiOutput& d) { return d.get(true).sent.take(); },
			"The messages Rack's ports sent since the last call, in order (up to 1024 wait; more are dropped).")
		.def_property_readonly("dropped", [](const PythonMidiOutput& d) { return d.get(true).sent.dropped.load(); });

	py::class_<PythonMidiDriver>(mod, "PythonDriver", py::is_final(),
		"A MIDI driver Python configures: devices whose calls native code answers. Give it to Rack with add_driver; "
		"Rack keeps it until it quits.")
		.def(py::init([](std::string name) { return new PythonMidiDriver(std::move(name)); }), py::arg("name"))
		.def_property_readonly("registered_id", [](const PythonMidiDriver& d) { return d.registeredId; })
		.def_property("name", [](const PythonMidiDriver& d) { return d.get().getName(); },
			[](const PythonMidiDriver& d, std::string name) {
				auto& driver = d.get();
				std::lock_guard<std::mutex> lock(driver.devicesMutex);
				driver.name = std::move(name);
			})
		.def("add_input_device", [](const PythonMidiDriver& d, std::string name) {
			auto& driver = d.get();
			auto device = std::make_unique<ConfiguredInput>();
			device->name = std::move(name);
			std::lock_guard<std::mutex> lock(driver.devicesMutex);
			int id = driver.nextInput++;
			driver.inputs[id] = std::move(device);
			if (driver.defaultInput < 0)
				driver.defaultInput = id;
			return PythonMidiInput{d.alive, d.driver, id};
		}, py::arg("name"), "A new input device (a hotplug, once Rack has the driver); the first is the default.")
		.def("add_output_device", [](const PythonMidiDriver& d, std::string name) {
			auto& driver = d.get();
			auto device = std::make_unique<ConfiguredOutput>();
			device->name = std::move(name);
			std::lock_guard<std::mutex> lock(driver.devicesMutex);
			int id = driver.nextOutput++;
			driver.outputs[id] = std::move(device);
			if (driver.defaultOutput < 0)
				driver.defaultOutput = id;
			return PythonMidiOutput{d.alive, d.driver, id};
		}, py::arg("name"))
		.def("remove_input_device", [](const PythonMidiDriver& d, const PythonMidiInput& device) { removeDevice(d, device); },
			py::arg("device"), "Unplugs it: Rack's lists no longer show it, and it is deleted once its last port leaves.")
		.def("remove_output_device", [](const PythonMidiDriver& d, const PythonMidiOutput& device) { removeDevice(d, device); },
			py::arg("device"));

	mod.def("add_driver", [](int id, PythonMidiDriver& d) {
		runtime::requireMainThread("adding a MIDI driver");
		if (m::getDriver(id))
			throw runtime::Ownership("Rack already has a MIDI driver with this ID (it is stored in patches)");
		if (!d.owned)
			throw runtime::Ownership("Rack already has this driver");
		m::addDriver(id, d.owned.release());  // Rack takes pointer ownership, until it quits
		d.registeredId = id;
	}, py::arg("driver_id"), py::arg("driver"),
		"Rack's addDriver for a PythonDriver: Rack owns it from now on, until it quits. The ID is stored in patches and "
		"must be unique.");

	mod.def("get_driver_ids", [] {
		runtime::requireMainThread("listing MIDI drivers");
		return m::getDriverIds();
	}, "Rack's: the IDs of its MIDI drivers.");
	mod.def("get_driver", [](int id) -> py::object {
		runtime::requireMainThread("finding a MIDI driver");
		return m::getDriver(id) ? py::cast(DriverView{id}) : py::none();
	}, py::arg("driver_id"), "Rack's: the driver with this ID, or None.");
}

void bindLaboratoryInputQueue(py::module_ lab) {
	using Q = LaboratoryInputQueue;
	py::class_<Q, m::Input, std::unique_ptr<Q, MainThreadDelete<Q>>>(lab, "MidiInputQueue",
		"Rack's InputQueue whose on_message a Python subclass overrides, offline: Rack's InputDevice::onMessage calls "
		"it synchronously on the main thread. super().on_message queues, as Rack's does. A rackpy.raw.midi.Input, not "
		"a rackpy.raw.midi.InputQueue (which is final).")
		.def(py::init([] {
			laboratory::requireEnabled("creating a laboratory MIDI input queue");
			requireLiveContext("laboratory.MidiInputQueue()");
			refuseDuringMidiDelivery("laboratory.MidiInputQueue()");
			return new Q;
		}))
		.def("on_message", [](Q& q, const m::Message& message) {
			checked(q);
			q.m::InputQueue::onMessage(message);
		}, py::arg("message"), "Rack's InputQueue::onMessage: queues the message (up to 8192).")
		.def("try_pop", [](Q& q, int64_t maxFrame) -> py::object {
			checked(q);
			m::Message message;
			if (!q.tryPop(&message, maxFrame))
				return py::none();
			return py::cast(message);
		}, py::arg("max_frame"), "Rack's: the next message if its frame is max_frame or earlier, else None.")
		.def("size", [](Q& q) {
			checked(q);
			return q.size();
		});
}

}  // namespace bindings
}  // namespace rackpy
