// SPDX-License-Identifier: GPL-3.0-or-later
// The laboratory MIDI driver and devices (RP-22C, RP-22F): Python overrides of
// every midi::Driver virtual, of midi::Device::getName and of
// midi::OutputDevice::sendMessage, which Rack's own midi::Port, Input and
// Output methods reach synchronously on the main thread. None is ever
// registered with Rack, so only Python-made ports that Python connects reach
// them; a Rack thread that did would run no Python (callPython refuses it)
// and get the native configured answer, which is also super(): the name and
// devices Python configured, as RP-29C's PythonDriver gives them. The
// laboratory MIDI inputs (MidiInput, MidiInputQueue) are in native/bindings/midi.cpp.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "devices.hpp"
#include "laboratory.hpp"
#include "../bindings/midi.hpp"
#include "../bindings/support/overrides.hpp"
#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace laboratory {

namespace {

namespace m = rack::midi;

struct InputDevice;
struct OutputDevice;
struct MidiDriver;
// Every live laboratory MIDI object, for liveMidiObjects (main thread).
std::vector<InputDevice*> inputDevices;
std::vector<OutputDevice*> outputDevices;
std::vector<MidiDriver*> drivers;

template <class T>
void forget(std::vector<T*>& all, T* object) {
	all.erase(std::remove(all.begin(), all.end(), object), all.end());
}

struct InputDevice : bindings::UnlistedMidiInputDevice {
	std::string name;
	InputDevice() { inputDevices.push_back(this); }
	~InputDevice() override { forget(inputDevices, this); }
	std::string getName() override { return name; }
};

struct PyInputDevice : InputDevice, py::trampoline_self_life_support {
	std::string getName() override {
		return dispatch<std::string>(static_cast<const InputDevice*>(this), "get_name",
			[this] { return InputDevice::getName(); },
			[](const bindings::PythonOverride& f) { return f().cast<std::string>(); });
	}
};

struct OutputDevice : bindings::UnlistedMidiOutputDevice {
	std::string name;
	OutputDevice() { outputDevices.push_back(this); }
	~OutputDevice() override { forget(outputDevices, this); }
	std::string getName() override { return name; }
};

struct PyOutputDevice : OutputDevice, py::trampoline_self_life_support {
	std::string getName() override {
		return dispatch<std::string>(static_cast<const OutputDevice*>(this), "get_name",
			[this] { return OutputDevice::getName(); },
			[](const bindings::PythonOverride& f) { return f().cast<std::string>(); });
	}
	void sendMessage(const m::Message& message) override {
		bindings::overrideOrVoid(static_cast<const OutputDevice*>(this), "send_message",
			[this, &message] { OutputDevice::sendMessage(message); }, m::Message(message));  // an owned copy
	}
};

/** A driver's devices of one direction, by ID. The driver keeps their Python objects alive, so a device it
returns from subscribe outlives the call. */
template <class Device>
struct Devices {
	struct Entry {
		py::object object;
		Device* device;
	};
	std::map<int, Entry> entries;
	int nextId = 0;
	int defaultId = -1;

	Device* find(int id) {
		auto it = entries.find(id);
		return it == entries.end() ? nullptr : it->second.device;
	}
	std::vector<int> ids() const {
		std::vector<int> out;
		for (auto& [id, entry] : entries)
			out.push_back(id);
		return out;
	}
	int defaultDevice() { return find(defaultId) ? defaultId : -1; }
	py::object object(const m::Device* device) const {
		for (auto& [id, entry] : entries)
			if (entry.device == device)
				return entry.object;
		return py::none();
	}
	/** The device a Python subscribe returned: one of these, or None. Needs the GIL. */
	Device* own(py::handle device, const char* what) {
		if (device.is_none())
			return nullptr;
		for (auto& [id, entry] : entries)
			if (entry.object.is(device))
				return entry.device;
		throw runtime::Ownership(std::string(what) + " must return one of this driver's devices, or None");
	}
	int add(py::object object) {
		if (!py::isinstance<Device>(object))
			throw py::type_error("this takes a laboratory MIDI device of the right direction");
		auto* device = &object.cast<Device&>();
		if (!this->object(device).is_none())
			throw runtime::Ownership("the driver already has this device");
		int id = nextId++;
		entries[id] = {std::move(object), device};
		if (defaultId < 0)
			defaultId = id;
		return id;
	}
	void remove(py::handle object) {
		bindings::refuseDuringMidiDelivery("removing a laboratory MIDI device");
		for (auto it = entries.begin(); it != entries.end(); ++it)
			if (it->second.object.is(object)) {
				it->second.device->disconnect();
				entries.erase(it);
				return;
			}
		throw runtime::Ownership("the driver does not have this device");
	}
};

/** rackpy.laboratory.MidiDriver: its native answers come from the devices Python added. */
struct MidiDriver : bindings::UnlistedMidiDriver {
	std::string name;
	Devices<InputDevice> inputs;
	Devices<OutputDevice> outputs;

	MidiDriver() { drivers.push_back(this); }
	~MidiDriver() override {
		forget(drivers, this);
		detach();  // the ports first: a device's __del__ may query them while the devices go
		auto dropInputs = std::move(inputs.entries);
		auto dropOutputs = std::move(outputs.entries);
		inputs.entries.clear();
		outputs.entries.clear();
	}

	std::string getName() override { return name; }
	std::vector<int> getInputDeviceIds() override { return inputs.ids(); }
	int getDefaultInputDeviceId() override { return inputs.defaultDevice(); }
	std::string getInputDeviceName(int id) override {
		auto* d = inputs.find(id);
		return d ? d->getName() : "";
	}
	m::InputDevice* subscribeInput(int id, m::Input* input) override {
		auto* d = inputs.find(id);
		if (!d)
			return nullptr;
		d->subscribe(input);
		return d;
	}
	void unsubscribeInput(int id, m::Input* input) override {
		if (auto* d = inputs.find(id))
			d->unsubscribe(input);
	}
	std::vector<int> getOutputDeviceIds() override { return outputs.ids(); }
	int getDefaultOutputDeviceId() override { return outputs.defaultDevice(); }
	std::string getOutputDeviceName(int id) override {
		auto* d = outputs.find(id);
		return d ? d->getName() : "";
	}
	m::OutputDevice* subscribeOutput(int id, m::Output* output) override {
		auto* d = outputs.find(id);
		if (!d)
			return nullptr;
		d->subscribe(output);
		return d;
	}
	void unsubscribeOutput(int id, m::Output* output) override {
		if (auto* d = outputs.find(id))
			d->unsubscribe(output);
	}
};

using Override = bindings::PythonOverride;

struct PyMidiDriver : MidiDriver, py::trampoline_self_life_support {
	const MidiDriver* self() const { return this; }
	std::string getName() override {
		return dispatch<std::string>(self(), "get_name", [this] { return MidiDriver::getName(); },
			[](const Override& f) { return f().cast<std::string>(); });
	}
	std::vector<int> getInputDeviceIds() override {
		return dispatch<std::vector<int>>(self(), "get_input_device_ids",
			[this] { return MidiDriver::getInputDeviceIds(); },
			[](const Override& f) { return f().cast<std::vector<int>>(); });
	}
	int getDefaultInputDeviceId() override {
		return dispatch<int>(self(), "get_default_input_device_id",
			[this] { return MidiDriver::getDefaultInputDeviceId(); }, [](const Override& f) { return f().cast<int>(); });
	}
	std::string getInputDeviceName(int id) override {
		return dispatch<std::string>(self(), "get_input_device_name",
			[this, id] { return MidiDriver::getInputDeviceName(id); },
			[id](const Override& f) { return f(id).cast<std::string>(); });
	}
	m::InputDevice* subscribeInput(int id, m::Input* input) override {
		return dispatch<m::InputDevice*>(self(), "subscribe_input",
			[input] { return bindings::livePythonMidiPort(input); },
			[this, id, input] { return MidiDriver::subscribeInput(id, input); },
			[this, id, input](const Override& f) {
				return static_cast<m::InputDevice*>(inputs.own(f(id, bindings::midiPortObject(input)), "subscribe_input"));
			});
	}
	void unsubscribeInput(int id, m::Input* input) override {
		dispatchVoid(self(), "unsubscribe_input", [input] { return bindings::livePythonMidiPort(input); },
			[this, id, input] { MidiDriver::unsubscribeInput(id, input); },
			[id, input](const Override& f) { f(id, bindings::midiPortObject(input)); });
	}
	std::vector<int> getOutputDeviceIds() override {
		return dispatch<std::vector<int>>(self(), "get_output_device_ids",
			[this] { return MidiDriver::getOutputDeviceIds(); },
			[](const Override& f) { return f().cast<std::vector<int>>(); });
	}
	int getDefaultOutputDeviceId() override {
		return dispatch<int>(self(), "get_default_output_device_id",
			[this] { return MidiDriver::getDefaultOutputDeviceId(); }, [](const Override& f) { return f().cast<int>(); });
	}
	std::string getOutputDeviceName(int id) override {
		return dispatch<std::string>(self(), "get_output_device_name",
			[this, id] { return MidiDriver::getOutputDeviceName(id); },
			[id](const Override& f) { return f(id).cast<std::string>(); });
	}
	m::OutputDevice* subscribeOutput(int id, m::Output* output) override {
		return dispatch<m::OutputDevice*>(self(), "subscribe_output",
			[output] { return bindings::livePythonMidiPort(output); },
			[this, id, output] { return MidiDriver::subscribeOutput(id, output); },
			[this, id, output](const Override& f) {
				return static_cast<m::OutputDevice*>(
					outputs.own(f(id, bindings::midiPortObject(output)), "subscribe_output"));
			});
	}
	void unsubscribeOutput(int id, m::Output* output) override {
		dispatchVoid(self(), "unsubscribe_output", [output] { return bindings::livePythonMidiPort(output); },
			[this, id, output] { MidiDriver::unsubscribeOutput(id, output); },
			[id, output](const Override& f) { f(id, bindings::midiPortObject(output)); });
	}
};

MidiDriver& driver(MidiDriver& d) {
	runtime::requireMainThread("using a laboratory MIDI driver");
	return d;
}

/** A Python port to subscribe or unsubscribe (`what`): refused during a synchronous delivery. */
template <class Port>
Port& port(py::handle value, const char* what) {
	bindings::refuseDuringMidiDelivery(what);
	auto* p = dynamic_cast<Port*>(&bindings::pythonMidiPort(value));
	if (!p)
		throw py::type_error(std::string(what) + " takes a RackPy MIDI port of the right direction");
	return *p;
}

void requireLiveContext(const char* what) {
	runtime::requireMainThread(what);
	auto token = runtime::binding().current();
	auto* c = rack::contextGet();
	if (!token || c != token->context || !c->engine)
		throw runtime::WrongContext(std::string(what) + " needs Rack's live bound UI context (Rack's InputDevice "
			"timestamps messages with its engine)");
}

int traverseDriver(PyObject* self, visitproc visit, void* arg) {
	Py_VISIT(Py_TYPE(self));
	if (auto* d = constructed<MidiDriver>(self)) {
		for (auto& [id, entry] : d->inputs.entries)
			Py_VISIT(entry.object.ptr());
		for (auto& [id, entry] : d->outputs.entries)
			Py_VISIT(entry.object.ptr());
	}
	return 0;
}

int clearDriver(PyObject* self) {
	if (auto* d = constructed<MidiDriver>(self)) {
		auto inputs = std::move(d->inputs.entries);  // a device that goes disconnects its ports
		auto outputs = std::move(d->outputs.entries);
		d->inputs.entries.clear();
		d->outputs.entries.clear();
	}
	return 0;
}

}  // namespace

template <class T>
void appendLive(const std::vector<T*>& all, std::vector<py::object>& out) {
	auto* info = py::detail::get_type_info(typeid(T));
	if (!info)
		return;
	for (T* object : all)
		if (py::handle h = py::detail::get_object_handle(object, info))  // none while it is being deallocated
			out.push_back(py::reinterpret_borrow<py::object>(h));
}

void liveMidiObjects(std::vector<py::object>& out) {
	appendLive(drivers, out);
	appendLive(inputDevices, out);
	appendLive(outputDevices, out);
}

#ifdef RACKPY_TEST_HOOKS
m::Driver& midiDriverForTesting(py::handle driver) {
	return driver.cast<MidiDriver&>();
}
m::Device& midiDeviceForTesting(py::handle device) {
	if (py::isinstance<InputDevice>(device))
		return device.cast<InputDevice&>();
	return device.cast<OutputDevice&>();
}
#endif

void bindMidi(py::module_ lab) {
	py::classh<InputDevice, PyInputDevice>(lab, "MidiInputDevice",
		"A MIDI input device whose get_name a Python subclass overrides, offline; add it to a MidiDriver.")
		.def(py::init([](std::string name) {
			requireEnabled("creating a laboratory MIDI device");
			runtime::requireMainThread("creating a laboratory MIDI device");
			auto d = std::make_unique<PyInputDevice>();
			d->name = std::move(name);
			return d;
		}), py::arg("name") = "")
		.def_property("name", [](InputDevice& d) {
				runtime::requireMainThread("using a laboratory MIDI device");
				return d.name;
			}, [](InputDevice& d, std::string name) {
				runtime::requireMainThread("using a laboratory MIDI device");
				d.name = std::move(name);
			}, "The configured name: what super().get_name() returns.")
		.def("get_name", [](InputDevice& d) {
			runtime::requireMainThread("using a laboratory MIDI device");
			return d.InputDevice::getName();
		}, "Rack's Device::getName: here, the configured name.")
		.def_property_readonly("subscribed", [](py::object self) {
			runtime::requireMainThread("using a laboratory MIDI device");
			return bindings::midiSubscribers(self, self.cast<InputDevice&>());
		}, "Rack's subscribers: the Python inputs subscribed to this device.")
		.def("push_message", [](InputDevice& d, const m::Message& message) {
			requireLiveContext("push_message");
			bindings::MidiDeliveryCall call;
			d.onMessage(message);
		}, py::arg("message"),
			"Rack's InputDevice::onMessage, as driver code calls it when a message arrives: each subscribed input's "
			"on_message (channel-filtered; an unset frame is set from Rack's engine timing). A laboratory input's "
			"override runs now, and its error is raised here.");

	py::classh<OutputDevice, PyOutputDevice>(lab, "MidiOutputDevice",
		"A MIDI output device whose send_message and get_name a Python subclass overrides; connect Python-made output "
		"ports to it, or add it to a MidiDriver.")
		.def(py::init([](std::string name) {
			requireEnabled("creating a laboratory MIDI device");
			runtime::requireMainThread("creating a laboratory MIDI device");
			auto d = std::make_unique<PyOutputDevice>();
			d->name = std::move(name);
			return d;
		}), py::arg("name") = "")
		.def_property("name", [](OutputDevice& d) {
				runtime::requireMainThread("using a laboratory MIDI device");
				return d.name;
			}, [](OutputDevice& d, std::string name) {
				runtime::requireMainThread("using a laboratory MIDI device");
				d.name = std::move(name);
			}, "The configured name: what super().get_name() returns.")
		.def("get_name", [](OutputDevice& d) {
			runtime::requireMainThread("using a laboratory MIDI device");
			return d.OutputDevice::getName();
		}, "Rack's Device::getName: here, the configured name.")
		.def("send_message", [](OutputDevice& d, const m::Message& message) {
			runtime::requireMainThread("using a laboratory MIDI device");
			d.OutputDevice::sendMessage(message);
		}, py::arg("message"), "Rack's OutputDevice::sendMessage: does nothing; a subclass receives each message here.")
		.def("connect", [](OutputDevice& d, py::object output) {
			requireEnabled("connecting a laboratory MIDI device");
			bindings::connectOutputDevice(output, d);
		}, py::arg("output"),
			"Point a rackpy.raw.midi.Output you made at this device, instead of a driver's: its send_message then "
			"reaches this device's send_message, with the port's channel applied, as Rack's Output::sendMessage does.")
		.def_property_readonly("connected", [](const OutputDevice& d) { return d.subscribed.size(); },
			"How many ports point at this device.");

	py::classh<MidiDriver, PyMidiDriver>(lab, "MidiDriver", py::custom_type_setup([](PyHeapTypeObject* heap) {
		auto* type = &heap->ht_type;
		type->tp_flags |= Py_TPFLAGS_HAVE_GC;
		type->tp_traverse = traverseDriver;
		type->tp_clear = clearDriver;
	}), "A MIDI driver whose Rack virtuals a Python subclass overrides, offline. Rack never lists it: connect(port) "
		"points a rackpy.raw.midi port you made at it.")
		.def(py::init([](std::string name) {
			requireEnabled("creating a laboratory MIDI driver");
			runtime::requireMainThread("creating a laboratory MIDI driver");
			auto d = std::make_unique<PyMidiDriver>();
			d->name = std::move(name);
			return d;
		}), py::arg("name") = "")
		.def_property("name", [](MidiDriver& d) { return driver(d).name; },
			[](MidiDriver& d, std::string name) { driver(d).name = std::move(name); },
			"The configured name: what super().get_name() returns.")
		.def("add_input_device", [](MidiDriver& d, py::object device) { return driver(d).inputs.add(device); },
			py::arg("device"), "Give the driver a MidiInputDevice; returns its ID. The first is the default.")
		.def("add_output_device", [](MidiDriver& d, py::object device) { return driver(d).outputs.add(device); },
			py::arg("device"), "Give the driver a MidiOutputDevice; returns its ID. The first is the default.")
		.def("remove_input_device", [](MidiDriver& d, py::object device) { driver(d).inputs.remove(device); },
			py::arg("device"),
			"Unplug a device: every Python port using it leaves it and points nowhere (natively), and the driver "
			"lets it go.")
		.def("remove_output_device", [](MidiDriver& d, py::object device) { driver(d).outputs.remove(device); },
			py::arg("device"))
		.def_property("default_input_device_id", [](MidiDriver& d) { return driver(d).inputs.defaultId; },
			[](MidiDriver& d, int id) { driver(d).inputs.defaultId = id; },
			"What super().get_default_input_device_id() returns, if the driver has that device (else -1).")
		.def_property("default_output_device_id", [](MidiDriver& d) { return driver(d).outputs.defaultId; },
			[](MidiDriver& d, int id) { driver(d).outputs.defaultId = id; })
		.def("get_input_device", [](MidiDriver& d, int id) {
			return driver(d).inputs.object(d.inputs.find(id));
		}, py::arg("device_id"), "The input device with this ID, or None.")
		.def("get_output_device", [](MidiDriver& d, int id) {
			return driver(d).outputs.object(d.outputs.find(id));
		}, py::arg("device_id"))
		.def("connect", [](MidiDriver& d, py::object p) {
			requireEnabled("connecting a laboratory MIDI driver");
			bindings::connectMidiDriver(p, driver(d));
		}, py::arg("port"),
			"Point a rackpy.raw.midi Input, InputQueue or Output you made at this driver, as Rack's Port::setDriverId "
			"does for a driver it lists: the port leaves its device, takes this driver, then its default device. "
			"Rack's own port methods then call this driver's and its devices' virtuals.")
		// Rack's virtuals, native (super()): answers from the devices added.
		.def("get_name", [](MidiDriver& d) { return driver(d).MidiDriver::getName(); })
		.def("get_input_device_ids", [](MidiDriver& d) { return driver(d).MidiDriver::getInputDeviceIds(); })
		.def("get_default_input_device_id", [](MidiDriver& d) {
			return driver(d).MidiDriver::getDefaultInputDeviceId();
		})
		.def("get_input_device_name", [](MidiDriver& d, int id) {
			return driver(d).MidiDriver::getInputDeviceName(id);
		}, py::arg("device_id"), "The device's get_name(), or \"\".")
		.def("subscribe_input", [](MidiDriver& d, int id, py::object input) {
			m::InputDevice* device = driver(d).MidiDriver::subscribeInput(id, &port<m::Input>(input, "subscribe_input"));
			return d.inputs.object(device);
		}, py::arg("device_id"), py::arg("input"),
			"Subscribes the input to the device (Rack's InputDevice::subscribe), then the device, or None if there is "
			"none. An override must return one of this driver's input devices or None.")
		.def("unsubscribe_input", [](MidiDriver& d, int id, py::object input) {
			driver(d).MidiDriver::unsubscribeInput(id, &port<m::Input>(input, "unsubscribe_input"));
		}, py::arg("device_id"), py::arg("input"))
		.def("get_output_device_ids", [](MidiDriver& d) { return driver(d).MidiDriver::getOutputDeviceIds(); })
		.def("get_default_output_device_id", [](MidiDriver& d) {
			return driver(d).MidiDriver::getDefaultOutputDeviceId();
		})
		.def("get_output_device_name", [](MidiDriver& d, int id) {
			return driver(d).MidiDriver::getOutputDeviceName(id);
		}, py::arg("device_id"))
		.def("subscribe_output", [](MidiDriver& d, int id, py::object output) {
			m::OutputDevice* device =
				driver(d).MidiDriver::subscribeOutput(id, &port<m::Output>(output, "subscribe_output"));
			return d.outputs.object(device);
		}, py::arg("device_id"), py::arg("output"))
		.def("unsubscribe_output", [](MidiDriver& d, int id, py::object output) {
			driver(d).MidiDriver::unsubscribeOutput(id, &port<m::Output>(output, "unsubscribe_output"));
		}, py::arg("device_id"), py::arg("output"));

	lab.def("mark_midi_input", [](py::handle input) {
		requireEnabled("creating a laboratory MIDI input");
		runtime::requireMainThread("creating a laboratory MIDI input");
		bindings::markLaboratoryInput(input);
	}, py::arg("input"), "Make a Python-made rackpy.raw.midi.Input a laboratory input (rackpy.laboratory.MidiInput).");
	lab.def("is_laboratory_midi_input", [](py::handle input) { return bindings::isLaboratoryInput(input); },
		py::arg("input"));
	bindings::bindLaboratoryInputQueue(lab);
}

}  // namespace laboratory
}  // namespace rackpy
