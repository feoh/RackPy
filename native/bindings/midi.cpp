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
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rack.hpp>

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
		this->driver = nullptr;
		this->driverId = -1;
	}
};

/** A Python Input: on_message runs on the UI thread, from the queue its driver thread fills. */
struct PyInput : TrackedPort<m::Input> {
	MessageQueue queue;
	void onMessage(const m::Message& message) override { queue.push(message); }
};
using PyInputQueue = TrackedPort<m::InputQueue>;
using PyOutput = TrackedPort<m::Output>;

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
				py::function f = py::get_override(self, "on_message");
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
	py::object device;  // the DeviceView it was listed by
	const m::Port* port;
	bool input;
	const m::Port& get() const {
		auto& view = device.cast<const DeviceView&>();
		m::Device& d = view.get();
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

template <class T>
T& pythonPort(py::handle value, const char* what) {
	if (!py::isinstance<T>(value))
		throw py::type_error(std::string(what) + " takes a RackPy MIDI port of the right direction");
	auto& port = value.cast<T&>();
	tracked(port);
	return port;
}

template <class Set>
py::list subscribers(py::object deviceView, const Set& set, bool input) {
	py::list out;
	for (auto* port : set) {
		if (py::object o = portObject(port))
			out.append(o);
		else
			out.append(py::cast(PortView{deviceView, port, input}));
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

}  // namespace

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
			return subscribers(self, static_cast<m::InputDevice&>(v.get()).subscribed, true);
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
			static_cast<m::InputDevice&>(v.get()).onMessage(message);
		}, py::arg("message"),
			"Rack's: delivers the message to the device's subscribers, as the driver does when one arrives (an unset "
			"frame is set to Rack's engine timing).");
	py::class_<OutputDeviceView, DeviceView>(mod, "OutputDevice")
		.def_property_readonly("subscribed", [](py::object self) {
			auto& v = self.cast<const OutputDeviceView&>();
			return subscribers(self, static_cast<m::OutputDevice&>(v.get()).subscribed, false);
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
		.def("set_driver_id", [](m::Port& p, int id) { checked(p).setDriverId(id); }, py::arg("driver_id"),
			"Rack's: the driver (an unknown ID falls back to Rack's first), then its default device.")
		.def("get_device", [](py::object self) {
			auto& p = checked(self.cast<m::Port&>());
			return deviceObject(self, p.getDevice(), dynamic_cast<m::Input*>(&p) != nullptr);
		})
		.def("get_device_id", [](m::Port& p) { return checked(p).getDeviceId(); })
		.def("set_device_id", [](m::Port& p, int id) { checked(p).setDeviceId(id); }, py::arg("device_id"),
			"Rack's: subscribes to the driver's device (-1: none).")
		.def("get_device_ids", [](m::Port& p) { return checked(p).getDeviceIds(); })
		.def("get_default_device_id", [](m::Port& p) { return checked(p).getDefaultDeviceId(); })
		.def("get_device_name", [](m::Port& p, int id) { return checked(p).getDeviceName(id); }, py::arg("device_id"))
		.def("get_channels", [](m::Port& p) { return checked(p).getChannels(); })
		.def("get_channel", [](m::Port& p) { return checked(p).getChannel(); })
		.def("set_channel", [](m::Port& p, int channel) { checked(p).setChannel(channel); }, py::arg("channel"))
		.def("get_channel_name", [](m::Port& p, int channel) { return checked(p).getChannelName(channel); },
			py::arg("channel"))
		.def("to_json", [](m::Port& p) {
			json_t* data = checked(p).toJson();
			std::unique_ptr<json_t, decltype(&json_decref)> held(data, json_decref);
			return runtime::fromJson(data);
		})
		.def("from_json", [](m::Port& p, py::handle data) {
			checked(p);
			std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(data), json_decref);
			p.fromJson(json.get());
		}, py::arg("data"), "Rack's: the driver, then the device with the saved name, then the channel.");

	py::class_<m::Input, m::Port, PyInput, std::unique_ptr<m::Input, MainThreadDelete<m::Input>>>(mod, "Input",
		"Rack's MIDI input port. Subclass it and override on_message: RackPy calls it on the UI thread with each "
		"message the driver delivered (queued; up to 1024 wait, more are dropped and counted).")
		.def(py::init([] {
			requireLiveContext("midi.Input()");
			hookDelivery();
			return new PyInput;
		}))
		.def_property_readonly("input_device", [](py::object self) {
			auto& p = static_cast<m::Input&>(checked(self.cast<m::Port&>()));
			return deviceObject(self, p.inputDevice, true);
		})
		.def("reset", [](m::Input& p) { checked(p); p.reset(); }, "Rack's: Rack's first driver and its default device, all channels.")
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
			return static_cast<m::InputQueue*>(new PyInputQueue);
		}))
		.def("on_message", [](m::InputQueue& q, const m::Message& message) {
			checked(q);
			q.InputQueue::onMessage(message);
		}, py::arg("message"), "Rack's: queues the message, as the driver does.")
		.def("try_pop", [](m::InputQueue& q, int64_t maxFrame) -> py::object {
			checked(q);
			m::Message message;
			if (!q.tryPop(&message, maxFrame))
				return py::none();
			return py::cast(message);
		}, py::arg("max_frame"), "Rack's: the next message if its frame is max_frame or earlier, else None.")
		.def("size", [](m::InputQueue& q) {
			checked(q);
			return q.size();
		});

	py::class_<m::Output, m::Port, PyOutput, std::unique_ptr<m::Output, MainThreadDelete<m::Output>>>(mod, "Output",
		"Rack's MIDI output port. A subclass may override Rack's port virtuals (get_device_ids, get_channels, ...), "
		"which Rack's menus call on the UI thread.")
		.def(py::init([] {
			requireLiveContext("midi.Output()");
			return new PyOutput;
		}))
		.def_property_readonly("output_device", [](py::object self) {
			auto& p = static_cast<m::Output&>(checked(self.cast<m::Port&>()));
			return deviceObject(self, p.outputDevice, false);
		})
		.def("reset", [](m::Output& p) { checked(p); p.reset(); }, "Rack's: Rack's first driver and its default device, channel 1.")
		.def("send_message", [](m::Output& p, const m::Message& message) {
			checked(p);
			requireLiveContext("Output.send_message");
			p.sendMessage(message);
		}, py::arg("message"), "Rack's: sends to the port's device, with the port's channel unless it is -1.");

	mod.def("get_driver_ids", [] {
		runtime::requireMainThread("listing MIDI drivers");
		return m::getDriverIds();
	}, "Rack's: the IDs of its MIDI drivers.");
	mod.def("get_driver", [](int id) -> py::object {
		runtime::requireMainThread("finding a MIDI driver");
		return m::getDriver(id) ? py::cast(DriverView{id}) : py::none();
	}, py::arg("driver_id"), "Rack's: the driver with this ID, or None.");
}

}  // namespace bindings
}  // namespace rackpy
