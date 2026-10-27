// SPDX-License-Identifier: GPL-3.0-or-later
// The laboratory audio driver and device (RP-22F): Python overrides of every
// audio::Driver and audio::Device virtual. Rack's own audio::Port methods call
// them synchronously on the main thread (setDeviceId subscribes through the
// driver, getSampleRate asks the device, ...). Neither is ever registered with
// Rack, so only Python-made ports that driver.connect(port) points at the
// driver reach them; a Rack thread that did would run no Python (callPython
// refuses it) and get the native configured answer. super() is that answer:
// the values Python configured, as RP-29C's PythonDriver and PythonDevice
// give them.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "devices.hpp"
#include "laboratory.hpp"
#include "../bindings/audio.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace laboratory {

namespace {

namespace a = rack::audio;

struct AudioDevice;
struct AudioDriver;
// Every live laboratory audio object, for liveAudioObjects (main thread).
std::vector<AudioDevice*> audioDevices;
std::vector<AudioDriver*> audioDrivers;

template <class T>
void forget(std::vector<T*>& all, T* object) {
	all.erase(std::remove(all.begin(), all.end(), object), all.end());
}

/** rackpy.laboratory.AudioDevice: its native answers are the values Python configured. */
struct AudioDevice : bindings::UnlistedAudioDevice {
	AudioDevice() { audioDevices.push_back(this); }
	~AudioDevice() override { forget(audioDevices, this); }
};

struct PyAudioDevice : AudioDevice, py::trampoline_self_life_support {
	const AudioDevice* self() const { return this; }
	std::string getName() override {
		return dispatch<std::string>(self(), "get_name", [this] { return AudioDevice::getName(); },
			[](const bindings::PythonOverride& f) { return f().cast<std::string>(); });
	}
	int getNumInputs() override {
		return dispatch<int>(self(), "get_num_inputs", [this] { return AudioDevice::getNumInputs(); },
			[](const bindings::PythonOverride& f) { return f().cast<int>(); });
	}
	int getNumOutputs() override {
		return dispatch<int>(self(), "get_num_outputs", [this] { return AudioDevice::getNumOutputs(); },
			[](const bindings::PythonOverride& f) { return f().cast<int>(); });
	}
	std::set<float> getSampleRates() override {
		return dispatch<std::set<float>>(self(), "get_sample_rates", [this] { return AudioDevice::getSampleRates(); },
			[](const bindings::PythonOverride& f) { return valueSet<float>(f()); });
	}
	float getSampleRate() override {
		return dispatch<float>(self(), "get_sample_rate", [this] { return AudioDevice::getSampleRate(); },
			[](const bindings::PythonOverride& f) { return f().cast<float>(); });
	}
	void setSampleRate(float rate) override {
		dispatchVoid(self(), "set_sample_rate", [] { return true; }, [this, rate] { AudioDevice::setSampleRate(rate); },
			[rate](const bindings::PythonOverride& f) { f(rate); });
	}
	std::set<int> getBlockSizes() override {
		return dispatch<std::set<int>>(self(), "get_block_sizes", [this] { return AudioDevice::getBlockSizes(); },
			[](const bindings::PythonOverride& f) { return valueSet<int>(f()); });
	}
	int getBlockSize() override {
		return dispatch<int>(self(), "get_block_size", [this] { return AudioDevice::getBlockSize(); },
			[](const bindings::PythonOverride& f) { return f().cast<int>(); });
	}
	void setBlockSize(int size) override {
		dispatchVoid(self(), "set_block_size", [] { return true; }, [this, size] { AudioDevice::setBlockSize(size); },
			[size](const bindings::PythonOverride& f) { f(size); });
	}
	void subscribe(a::Port* port) override {
		dispatchSubscriptionVoid(self(), "subscribe", [port] { return bindings::livePythonAudioPort(port); },
			[this, port] { AudioDevice::subscribe(port); },
			[port](const bindings::PythonOverride& f) { f(bindings::audioPortObject(port)); });
	}
	void unsubscribe(a::Port* port) override {
		dispatchSubscriptionVoid(self(), "unsubscribe", [port] { return bindings::livePythonAudioPort(port); },
			[this, port] { AudioDevice::unsubscribe(port); },
			[port](const bindings::PythonOverride& f) { f(bindings::audioPortObject(port)); });
	}
};

/** rackpy.laboratory.AudioDriver: its native answers come from the devices Python added. The Python objects of
its devices stay alive while it holds them, so a device it returns from subscribe outlives the call. */
struct AudioDriver : bindings::UnlistedAudioDriver {
	struct Entry {
		py::object object;
		AudioDevice* device;
	};
	std::string name;
	std::map<int, Entry> devices;
	int nextId = 0;
	int defaultId = -1;

	AudioDriver() { audioDrivers.push_back(this); }
	~AudioDriver() override {
		forget(audioDrivers, this);
		detach();  // the ports first: a device's __del__ may query them while the devices go
		auto drop = std::move(devices);
		devices.clear();
	}

	AudioDevice* find(int id) {
		auto it = devices.find(id);
		return it == devices.end() ? nullptr : it->second.device;
	}
	/** The device a Python subscribe returned: one of this driver's, or None. Needs the GIL. */
	a::Device* own(py::handle device) {
		if (device.is_none())
			return nullptr;
		for (auto& [id, entry] : devices)
			if (entry.object.is(device))
				return entry.device;
		throw runtime::Ownership("subscribe must return one of this driver's devices (add_device), or None");
	}
	py::object object(a::Device* device) {
		for (auto& [id, entry] : devices)
			if (entry.device == device)
				return entry.object;
		return py::none();
	}

	std::string getName() override { return name; }
	std::vector<int> getDeviceIds() override {
		std::vector<int> ids;
		for (auto& [id, entry] : devices)
			ids.push_back(id);
		return ids;
	}
	int getDefaultDeviceId() override { return find(defaultId) ? defaultId : -1; }
	std::string getDeviceName(int id) override {
		auto* d = find(id);
		return d ? d->getName() : "";
	}
	int getDeviceNumInputs(int id) override {
		auto* d = find(id);
		return d ? d->getNumInputs() : 0;
	}
	int getDeviceNumOutputs(int id) override {
		auto* d = find(id);
		return d ? d->getNumOutputs() : 0;
	}
	a::Device* subscribe(int id, a::Port* port) override {
		auto* d = find(id);
		if (!d)
			return nullptr;  // Rack's port keeps no device
		d->subscribe(port);  // virtual: a Python override may remove the device, which may then be gone
		return find(id) == d ? d : nullptr;  // IDs are never reused
	}
	void unsubscribe(int id, a::Port* port) override {
		if (auto* d = find(id))
			d->unsubscribe(port);
	}
};

struct PyAudioDriver : AudioDriver, py::trampoline_self_life_support {
	const AudioDriver* self() const { return this; }
	std::string getName() override {
		return dispatch<std::string>(self(), "get_name", [this] { return AudioDriver::getName(); },
			[](const bindings::PythonOverride& f) { return f().cast<std::string>(); });
	}
	std::vector<int> getDeviceIds() override {
		return dispatch<std::vector<int>>(self(), "get_device_ids", [this] { return AudioDriver::getDeviceIds(); },
			[](const bindings::PythonOverride& f) { return f().cast<std::vector<int>>(); });
	}
	int getDefaultDeviceId() override {
		return dispatch<int>(self(), "get_default_device_id", [this] { return AudioDriver::getDefaultDeviceId(); },
			[](const bindings::PythonOverride& f) { return f().cast<int>(); });
	}
	std::string getDeviceName(int id) override {
		return dispatch<std::string>(self(), "get_device_name", [this, id] { return AudioDriver::getDeviceName(id); },
			[id](const bindings::PythonOverride& f) { return f(id).cast<std::string>(); });
	}
	int getDeviceNumInputs(int id) override {
		return dispatch<int>(self(), "get_device_num_inputs", [this, id] { return AudioDriver::getDeviceNumInputs(id); },
			[id](const bindings::PythonOverride& f) { return f(id).cast<int>(); });
	}
	int getDeviceNumOutputs(int id) override {
		return dispatch<int>(self(), "get_device_num_outputs",
			[this, id] { return AudioDriver::getDeviceNumOutputs(id); },
			[id](const bindings::PythonOverride& f) { return f(id).cast<int>(); });
	}
	a::Device* subscribe(int id, a::Port* port) override {
		return dispatchSubscription<a::Device*>(self(), "subscribe", [port] { return bindings::livePythonAudioPort(port); },
			[this, id, port] { return AudioDriver::subscribe(id, port); },
			[this, id, port](const bindings::PythonOverride& f) { return own(f(id, bindings::audioPortObject(port))); });
	}
	void unsubscribe(int id, a::Port* port) override {
		dispatchSubscriptionVoid(self(), "unsubscribe", [port] { return bindings::livePythonAudioPort(port); },
			[this, id, port] { AudioDriver::unsubscribe(id, port); },
			[id, port](const bindings::PythonOverride& f) { f(id, bindings::audioPortObject(port)); });
	}
};

AudioDevice& device(AudioDevice& d) {
	runtime::requireMainThread("using a laboratory audio device");
	return d;
}

AudioDriver& driver(AudioDriver& d) {
	runtime::requireMainThread("using a laboratory audio driver");
	return d;
}

std::vector<float> sorted(const std::set<float>& values) {
	return std::vector<float>(values.begin(), values.end());
}
std::vector<int> sorted(const std::set<int>& values) {
	return std::vector<int>(values.begin(), values.end());
}

int traverseDriver(PyObject* self, visitproc visit, void* arg) {
	Py_VISIT(Py_TYPE(self));
	if (auto* d = constructed<AudioDriver>(self))
		for (auto& [id, entry] : d->devices)
			Py_VISIT(entry.object.ptr());
	return 0;
}

int clearDriver(PyObject* self) {
	// Off the main thread (a Python thread's collection), Rack's UI may be reading the driver: leave the cycle to
	// its other members' clearing, or to a collection on the main thread.
	if (!runtime::isMainThread())
		return 0;
	if (auto* d = constructed<AudioDriver>(self)) {
		auto devices = std::move(d->devices);  // a device that goes disconnects its ports
		d->devices.clear();
	}
	return 0;
}

}  // namespace

template <class T>
void appendLive(const std::vector<T*>& all, std::vector<py::object>& out) {
	auto* info = py::detail::get_type_info(typeid(T));
	if (!info)
		return;
	for (T* object : all) {
		py::handle h = py::detail::get_object_handle(object, info);  // refcount 0: being deallocated
		if (h && Py_REFCNT(h.ptr()) > 0)
			out.push_back(py::reinterpret_borrow<py::object>(h));
	}
}

void liveAudioObjects(std::vector<py::object>& out) {
	appendLive(audioDrivers, out);
	appendLive(audioDevices, out);
}

#ifdef RACKPY_TEST_HOOKS
a::Driver& audioDriverForTesting(py::handle driver) {
	return driver.cast<AudioDriver&>();
}
a::Device& audioDeviceForTesting(py::handle device) {
	return device.cast<AudioDevice&>();
}
#endif

void bindAudio(py::module_ lab) {
	py::classh<AudioDevice, PyAudioDevice>(lab, "AudioDevice",
		"An audio device whose Rack virtuals a Python subclass overrides, offline; add it to an AudioDriver.")
		.def(py::init([](std::string name, int numInputs, int numOutputs, std::vector<float> rates, float rate,
						  std::vector<int> sizes, int size) {
			requireEnabled("creating a laboratory audio device");
			runtime::requireMainThread("creating a laboratory audio device");
			if (numInputs < 0 || numOutputs < 0 || !(rate > 0.f) || size < 1)
				throw std::invalid_argument("channel counts are not negative, and the rate and block size are positive");
			std::unique_ptr<PyAudioDevice, MainThreadDeleteLater<PyAudioDevice>> d(new PyAudioDevice);
			d->name = std::move(name);
			d->numInputs.store(numInputs);
			d->numOutputs.store(numOutputs);
			d->sampleRates = std::set<float>(rates.begin(), rates.end());
			d->sampleRate.store(rate);
			d->blockSizes = std::set<int>(sizes.begin(), sizes.end());
			d->blockSize.store(size);
			return d;
		}), py::arg("name") = "", py::arg("num_inputs") = 2, py::arg("num_outputs") = 2,
			py::arg("sample_rates") = std::vector<float>{44100.f, 48000.f}, py::arg("sample_rate") = 48000.f,
			py::arg("block_sizes") = std::vector<int>{64, 128, 256, 512}, py::arg("block_size") = 256)
		.def_property("name", [](AudioDevice& d) { return device(d).AudioDevice::getName(); },
			[](AudioDevice& d, std::string name) {
				std::lock_guard<std::mutex> lock(device(d).fieldsMutex);
				d.name = std::move(name);
			}, "The configured name: what super().get_name() returns.")
		.def_property("num_inputs", [](AudioDevice& d) { return device(d).numInputs.load(); },
			[](AudioDevice& d, int n) {
				if (n < 0)
					throw std::invalid_argument("channel counts are not negative");
				device(d).numInputs.store(n);
			})
		.def_property("num_outputs", [](AudioDevice& d) { return device(d).numOutputs.load(); },
			[](AudioDevice& d, int n) {
				if (n < 0)
					throw std::invalid_argument("channel counts are not negative");
				device(d).numOutputs.store(n);
			})
		.def_property("sample_rates", [](AudioDevice& d) { return sorted(device(d).AudioDevice::getSampleRates()); },
			[](AudioDevice& d, std::vector<float> rates) {
				std::lock_guard<std::mutex> lock(device(d).fieldsMutex);
				d.sampleRates = std::set<float>(rates.begin(), rates.end());
			})
		.def_property("sample_rate", [](AudioDevice& d) { return device(d).sampleRate.load(); },
			[](AudioDevice& d, float rate) {
				if (!(rate > 0.f))
					throw std::invalid_argument("the sample rate must be positive");
				device(d).sampleRate.store(rate);
			})
		.def_property("block_sizes", [](AudioDevice& d) { return sorted(device(d).AudioDevice::getBlockSizes()); },
			[](AudioDevice& d, std::vector<int> sizes) {
				std::lock_guard<std::mutex> lock(device(d).fieldsMutex);
				d.blockSizes = std::set<int>(sizes.begin(), sizes.end());
			})
		.def_property("block_size", [](AudioDevice& d) { return device(d).blockSize.load(); },
			[](AudioDevice& d, int size) {
				if (size < 1)
					throw std::invalid_argument("the block size must be positive");
				device(d).blockSize.store(size);
			})
		.def("take_requests", [](AudioDevice& d) {
			std::lock_guard<std::mutex> lock(device(d).fieldsMutex);
			std::vector<std::pair<std::string, double>> out;
			out.swap(d.requests);
			return out;
		}, "The native set_sample_rate and set_block_size calls since the last call, oldest first.")
		.def_property_readonly("subscribed", [](py::object self) {
			return bindings::audioSubscribers(self, device(self.cast<AudioDevice&>()));
		}, "Rack's subscribers: the Python ports subscribed to this device.")
		// Rack's virtuals, native (super()): each is the configured answer.
		.def("get_name", [](AudioDevice& d) { return device(d).AudioDevice::getName(); })
		.def("get_num_inputs", [](AudioDevice& d) { return device(d).AudioDevice::getNumInputs(); })
		.def("get_num_outputs", [](AudioDevice& d) { return device(d).AudioDevice::getNumOutputs(); })
		.def("get_sample_rates", [](AudioDevice& d) { return sorted(device(d).AudioDevice::getSampleRates()); },
			"Sorted; an override may return any iterable of rates.")
		.def("get_sample_rate", [](AudioDevice& d) { return device(d).AudioDevice::getSampleRate(); })
		.def("set_sample_rate", [](AudioDevice& d, float rate) { device(d).AudioDevice::setSampleRate(rate); },
			py::arg("sample_rate"), "Sets sample_rate and records a request, as a PythonDevice does.")
		.def("get_block_sizes", [](AudioDevice& d) { return sorted(device(d).AudioDevice::getBlockSizes()); })
		.def("get_block_size", [](AudioDevice& d) { return device(d).AudioDevice::getBlockSize(); })
		.def("set_block_size", [](AudioDevice& d, int size) { device(d).AudioDevice::setBlockSize(size); },
			py::arg("block_size"))
		.def("subscribe", [](AudioDevice& d, py::object port) {
			device(d).a::Device::subscribe(&bindings::pythonAudioPort(port));
		}, py::arg("port"), "Rack's Device::subscribe: adds the port to subscribed (the driver's subscribe calls it).")
		.def("unsubscribe", [](AudioDevice& d, py::object port) {
			device(d).a::Device::unsubscribe(&bindings::pythonAudioPort(port));
		}, py::arg("port"), "Rack's Device::unsubscribe.");

	py::classh<AudioDriver, PyAudioDriver>(lab, "AudioDriver", py::custom_type_setup([](PyHeapTypeObject* heap) {
		auto* type = &heap->ht_type;
		type->tp_flags |= Py_TPFLAGS_HAVE_GC;
		type->tp_traverse = traverseDriver;
		type->tp_clear = clearDriver;
	}), "An audio driver whose Rack virtuals a Python subclass overrides, offline. Rack never lists it: connect(port) "
		"points a rackpy.raw.audio.Port you made at it.")
		.def(py::init([](std::string name) {
			requireEnabled("creating a laboratory audio driver");
			runtime::requireMainThread("creating a laboratory audio driver");
			std::unique_ptr<PyAudioDriver, MainThreadDeleteLater<PyAudioDriver>> d(new PyAudioDriver);
			d->name = std::move(name);
			return d;
		}), py::arg("name") = "")
		.def_property("name", [](AudioDriver& d) { return driver(d).name; },
			[](AudioDriver& d, std::string name) { driver(d).name = std::move(name); },
			"The configured name: what super().get_name() returns.")
		.def("add_device", [](py::object self, py::object dev) {
			auto& d = driver(self.cast<AudioDriver&>());
			if (!py::isinstance<AudioDevice>(dev))
				throw py::type_error("add_device takes a rackpy.laboratory.AudioDevice");
			if (!d.object(&dev.cast<AudioDevice&>()).is_none())
				throw runtime::Ownership("the driver already has this device");
			int id = d.nextId++;
			d.devices[id] = {dev, &dev.cast<AudioDevice&>()};
			if (d.defaultId < 0)
				d.defaultId = id;
			return id;
		}, py::arg("device"), "Give the driver a device; returns its ID. The first is the default. The driver keeps it.")
		.def("remove_device", [](AudioDriver& d, py::object dev) {
			driver(d);
			for (auto it = d.devices.begin(); it != d.devices.end(); ++it)
				if (it->second.object.is(dev)) {
					it->second.device->disconnect();
					d.devices.erase(it);
					return;
				}
			throw runtime::Ownership("the driver does not have this device");
		}, py::arg("device"),
			"Unplug a device: every Python port using it leaves it and points nowhere (natively), and the driver "
			"lets it go.")
		.def_property("default_device_id", [](AudioDriver& d) { return driver(d).defaultId; },
			[](AudioDriver& d, int id) { driver(d).defaultId = id; },
			"What super().get_default_device_id() returns, if the driver has that device (else -1).")
		.def("get_device", [](AudioDriver& d, int id) -> py::object {
			auto* dev = driver(d).find(id);
			return dev ? d.object(dev) : py::none();
		}, py::arg("device_id"), "The device with this ID, or None.")
		.def("connect", [](AudioDriver& d, py::object port) {
			requireEnabled("connecting a laboratory audio driver");
			LaboratoryCall call;
			bindings::connectAudioDriver(port, driver(d));
		}, py::arg("port"),
			"Point a rackpy.raw.audio.Port you made at this driver, as Rack's Port::setDriverId does for a driver it "
			"lists: the port leaves its device, takes this driver, then subscribes to get_default_device_id()'s "
			"device. Rack's own port methods then call this driver's and its devices' virtuals.")
		// Rack's virtuals, native (super()): answers from the devices added.
		.def("get_name", [](AudioDriver& d) { return driver(d).AudioDriver::getName(); })
		.def("get_device_ids", [](AudioDriver& d) { return driver(d).AudioDriver::getDeviceIds(); })
		.def("get_default_device_id", [](AudioDriver& d) { return driver(d).AudioDriver::getDefaultDeviceId(); })
		.def("get_device_name", [](AudioDriver& d, int id) { return driver(d).AudioDriver::getDeviceName(id); },
			py::arg("device_id"), "The device's get_name(), or \"\".")
		.def("get_device_num_inputs", [](AudioDriver& d, int id) {
			return driver(d).AudioDriver::getDeviceNumInputs(id);
		}, py::arg("device_id"))
		.def("get_device_num_outputs", [](AudioDriver& d, int id) {
			return driver(d).AudioDriver::getDeviceNumOutputs(id);
		}, py::arg("device_id"))
		.def("subscribe", [](AudioDriver& d, int id, py::object port) {
			a::Device* dev = driver(d).AudioDriver::subscribe(id, &bindings::pythonAudioPort(port));
			return d.object(dev);
		}, py::arg("device_id"), py::arg("port"),
			"The device's subscribe(port), then the device (None if there is no such device). An override must return "
			"one of this driver's devices or None.")
		.def("unsubscribe", [](AudioDriver& d, int id, py::object port) {
			driver(d).AudioDriver::unsubscribe(id, &bindings::pythonAudioPort(port));
		}, py::arg("device_id"), py::arg("port"), "The device's unsubscribe(port).");
}

}  // namespace laboratory
}  // namespace rackpy
