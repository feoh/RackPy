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
// Lifetimes: Rack's audio drivers live until audio::destroy(), which runs
// before RackPy finalizes Python, so every Python port gives its device back
// when Rack's context closes. A device belongs to its driver (RtAudio deletes
// one when its last port leaves), so a device view goes through its port.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rack.hpp>

#include "audio.hpp"
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

	void processBuffer(const float* input, int inputStride, float* output, int outputStride, int count) override {
		buffers.fetch_add(1, std::memory_order_relaxed);
		frames.fetch_add(static_cast<uint64_t>(count), std::memory_order_relaxed);
		lastFrames.store(count, std::memory_order_relaxed);
	}
	void processInput(const float* input, int inputStride, int count) override {
		inputCalls.fetch_add(1, std::memory_order_relaxed);
	}
	void processOutput(float* output, int outputStride, int count) override {
		outputCalls.fetch_add(1, std::memory_order_relaxed);
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
	void onStartStream() override { queue(StreamEvent::Start); }
	void onStopStream() override { queue(StreamEvent::Stop); }
};

std::vector<PyAudioPort*> ports;  // main thread
bool hooked = false;

/** Runs Python ports' on_start_stream and on_stop_stream for their queued events. Main thread, UI step. */
void deliverEvents() noexcept {
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
				if (py::function f = py::get_override(self, event == StreamEvent::Start ? "on_start_stream" : "on_stop_stream"))
					f();
			}
		});
	}
}

PyAudioPort::PyAudioPort() {
	ports.push_back(this);
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
		setDeviceId(-1);
	}
	catch (...) {
	}
	driver = nullptr;
	driverId = -1;
}

PyAudioPort& checked(a::Port& port) {
	runtime::requireMainThread("using an audio port");
	auto* p = dynamic_cast<PyAudioPort*>(&port);
	if (!p)
		throw runtime::Ownership("this audio port is not one Python made");
	if (p->released)
		throw runtime::StaleHandle("Rack's context closed: this audio port gave up its device");
	if (port.context != rack::contextGet())
		throw runtime::WrongContext("this audio port belongs to another Rack context");
	return *p;
}

/** One of Rack's audio drivers, by its ID: drivers live until Rack's shutdown. */
struct DriverView {
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
	return a::getDriver(id) ? py::cast(DriverView{id}) : py::none();
}

/** A device, through the Python port that holds it. */
struct DeviceView {
	py::object owner;
	a::Device* device;
	a::Device& get() const {
		auto& port = checked(owner.cast<a::Port&>());
		if (!device || !port.holds(device))
			throw runtime::StaleHandle("the port no longer holds this audio device");
		return *device;
	}
};

py::object deviceObject(py::object owner, a::Device* device) {
	return device ? py::cast(DeviceView{std::move(owner), device}) : py::none();
}

/** A port in a device's subscriber set that Python did not make (a module's): read-only. */
struct PortView {
	py::object device;
	const a::Port* port;
	const a::Port& get() const {
		a::Device& d = device.cast<const DeviceView&>().get();
		if (!d.subscribed.count(const_cast<a::Port*>(port)))
			throw runtime::StaleHandle("the port no longer subscribes to this device");
		return *port;
	}
};

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

template <class T>
std::vector<T> sorted(const std::set<T>& values) {
	return std::vector<T>(values.begin(), values.end());
}

}  // namespace

void bindAudio(py::module_ mod) {
	py::class_<DriverView>(mod, "Driver", py::is_final(), "One of Rack's audio drivers (get_driver), by its ID. Main thread.")
		.def_property_readonly("id", [](const DriverView& d) { return d.id; })
		.def("get_name", [](const DriverView& d) { return d.get().getName(); })
		.def("get_device_ids", [](const DriverView& d) { return d.get().getDeviceIds(); })
		.def("get_default_device_id", [](const DriverView& d) { return d.get().getDefaultDeviceId(); })
		.def("get_device_name", [](const DriverView& d, int id) { return d.get().getDeviceName(id); }, py::arg("device_id"))
		.def("get_device_num_inputs", [](const DriverView& d, int id) { return d.get().getDeviceNumInputs(id); },
			py::arg("device_id"))
		.def("get_device_num_outputs", [](const DriverView& d, int id) { return d.get().getDeviceNumOutputs(id); },
			py::arg("device_id"))
		.def("subscribe", [](const DriverView& d, int deviceId, py::object port) -> py::object {
			auto& p = pythonPort(port);
			a::Device* device = d.get().subscribe(deviceId, &p);
			if (!device)
				return py::none();
			p.driverSubscriptions.push_back({d.id, deviceId, device});
			return deviceObject(port, device);
		}, py::arg("device_id"), py::arg("port"),
			"Rack's: makes the port a subscriber of the device (which may open its stream), apart from the port's own "
			"device; RackPy undoes it when the port dies or the context closes. Port.set_device_id is the usual way.")
		.def("unsubscribe", [](const DriverView& d, int deviceId, py::object port) {
			auto& p = pythonPort(port);
			auto it = std::find_if(p.driverSubscriptions.begin(), p.driverSubscriptions.end(),
				[&](const PyAudioPort::Direct& s) { return s.driverId == d.id && s.deviceId == deviceId; });
			if (it == p.driverSubscriptions.end())
				throw runtime::Ownership("the port did not subscribe to this device through the driver");
			d.get().unsubscribe(deviceId, &p);
			p.driverSubscriptions.erase(it);
		}, py::arg("device_id"), py::arg("port"))
		.def("__eq__", [](const DriverView& x, const DriverView& y) { return x.id == y.id; })
		.def("__hash__", [](const DriverView& d) { return py::hash(py::int_(d.id)); })
		.def("__repr__", [](const DriverView& d) { return "<audio.Driver " + std::to_string(d.id) + ">"; });

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
		.def_property_readonly("input_offset", [](const PortView& v) { return v.get().inputOffset; })
		.def_property_readonly("output_offset", [](const PortView& v) { return v.get().outputOffset; })
		.def_property_readonly("max_inputs", [](const PortView& v) { return v.get().maxInputs; })
		.def_property_readonly("max_outputs", [](const PortView& v) { return v.get().maxOutputs; });

	py::class_<DeviceView>(mod, "Device", py::is_final(),
		"An audio device of a driver, through the Python port that holds it. Rack's device methods may raise Rack's "
		"Exception from the driver.")
		.def_property_readonly("alive", [](const DeviceView& v) {
			try {
				v.get();
				return true;
			}
			catch (const std::exception&) {
				return false;
			}
		})
		.def("get_name", [](const DeviceView& v) { return v.get().getName(); })
		.def("get_num_inputs", [](const DeviceView& v) { return v.get().getNumInputs(); })
		.def("get_num_outputs", [](const DeviceView& v) { return v.get().getNumOutputs(); })
		.def("get_sample_rates", [](const DeviceView& v) { return sorted(v.get().getSampleRates()); },
			"Rack's set of selectable rates, sorted.")
		.def("get_sample_rate", [](const DeviceView& v) { return v.get().getSampleRate(); })
		.def("set_sample_rate", [](const DeviceView& v, float rate) {
			a::Device& d = v.get();
			py::gil_scoped_release release;  // the driver may reopen its stream
			d.setSampleRate(rate);
		}, py::arg("sample_rate"))
		.def("get_block_sizes", [](const DeviceView& v) { return sorted(v.get().getBlockSizes()); })
		.def("get_block_size", [](const DeviceView& v) { return v.get().getBlockSize(); })
		.def("set_block_size", [](const DeviceView& v, int size) {
			a::Device& d = v.get();
			py::gil_scoped_release release;
			d.setBlockSize(size);
		}, py::arg("block_size"))
		.def_property_readonly("subscribed", [](py::object self) {
			a::Device& d = self.cast<const DeviceView&>().get();
			py::list out;
			std::lock_guard<std::mutex> lock(d.processMutex);  // Rack's: ports do not change while it is held
			for (a::Port* port : d.subscribed) {
				if (dynamic_cast<PyAudioPort*>(port))
					out.append(py::cast(port, py::return_value_policy::reference));
				else
					out.append(py::cast(PortView{self, port}));
			}
			return out;
		}, "Rack's subscribers: Python's own ports, and PortViews of the others (modules' ports).")
		.def("subscribe", [](const DeviceView& v, py::object port) {
			a::Device& d = v.get();
			auto& p = pythonPort(port);
			d.subscribe(&p);
			p.deviceSubscriptions.push_back(&d);
		}, py::arg("port"), "Rack's (its driver's subscribe calls it); RackPy undoes it with the port.")
		.def("unsubscribe", [](const DeviceView& v, py::object port) {
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
			return new PyAudioPort;
		}))
		.def_property("input_offset", [](a::Port& p) { return checked(p).inputOffset; },
			[](a::Port& p, int v) { checked(p).inputOffset = v; }, "Rack's: the device's first input channel for this port.")
		.def_property("output_offset", [](a::Port& p) { return checked(p).outputOffset; },
			[](a::Port& p, int v) { checked(p).outputOffset = v; })
		.def_property("max_inputs", [](a::Port& p) { return checked(p).maxInputs; },
			[](a::Port& p, int v) { checked(p).maxInputs = v; }, "Rack's: the most input channels this port processes.")
		.def_property("max_outputs", [](a::Port& p) { return checked(p).maxOutputs; },
			[](a::Port& p, int v) { checked(p).maxOutputs = v; })
		.def_property_readonly("driver_id", [](a::Port& p) { return checked(p).driverId; })
		.def_property_readonly("device_id", [](a::Port& p) { return checked(p).deviceId; })
		.def_property_readonly("driver", [](a::Port& p) { return checked(p).driver ? driverObject(p.driverId) : py::none(); })
		.def_property_readonly("device", [](py::object self) { return deviceObject(self, checked(self.cast<a::Port&>()).device); })
		.def_property_readonly("context", [](a::Port& p) -> py::object {
			checked(p);
			return py::module_::import("rackpy.raw").attr("context_get")();
		}, "Rack's context the port was made in (the live one: other contexts' ports are refused).")
		.def("reset", [](a::Port& p) { checked(p).reset(); }, "Rack's: Rack's first driver and its default device.")
		.def("get_driver", [](a::Port& p) { return checked(p).getDriver() ? driverObject(p.getDriverId()) : py::none(); })
		.def("get_driver_id", [](a::Port& p) { return checked(p).getDriverId(); })
		.def("set_driver_id", [](a::Port& p, int id) { checked(p).setDriverId(id); }, py::arg("driver_id"),
			"Rack's: the driver (an unknown ID falls back to Rack's first), then its default device.")
		.def("get_driver_name", [](a::Port& p) { return checked(p).getDriverName(); })
		.def("get_device", [](py::object self) { return deviceObject(self, checked(self.cast<a::Port&>()).getDevice()); })
		.def("get_device_ids", [](a::Port& p) { return checked(p).getDeviceIds(); })
		.def("get_device_id", [](a::Port& p) { return checked(p).getDeviceId(); })
		.def("set_device_id", [](a::Port& p, int id) { checked(p).setDeviceId(id); }, py::arg("device_id"),
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
			py::gil_scoped_release release;
			p.setSampleRate(rate);
		}, py::arg("sample_rate"))
		.def("get_block_sizes", [](a::Port& p) { return sorted(checked(p).getBlockSizes()); }, "Sorted.")
		.def("get_block_size", [](a::Port& p) { return checked(p).getBlockSize(); })
		.def("set_block_size", [](a::Port& p, int size) {
			checked(p);
			py::gil_scoped_release release;
			p.setBlockSize(size);
		}, py::arg("block_size"))
		.def("get_num_inputs", [](a::Port& p) { return checked(p).getNumInputs(); },
			"Rack's: the port's active inputs, after input_offset and max_inputs.")
		.def("get_num_outputs", [](a::Port& p) { return checked(p).getNumOutputs(); })
		.def("to_json", [](a::Port& p) {
			json_t* data = checked(p).toJson();
			std::unique_ptr<json_t, decltype(&json_decref)> held(data, json_decref);
			return runtime::fromJson(data);
		})
		.def("from_json", [](a::Port& p, py::handle data) {
			checked(p);
			std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(data), json_decref);
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
