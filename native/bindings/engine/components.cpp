// SPDX-License-Identifier: GPL-3.0-or-later
// Port, Input, Output, Param, Light, PortInfo and LightInfo -> rackpy.raw.engine (RP-13).
//
// Every method calls Rack's own inline implementation on the referenced
// component, after checking what Rack only asserts or assumes: channel
// indices (0 to PORT_MAX_CHANNELS - 1) and channel counts (at most
// PORT_MAX_CHANNELS; Rack's loops would run past the voltage array).
//
// Cross-thread access to a Rack-owned component is unsynchronized, exactly as
// Rack's own C++ UI code reads ports and lights: a value may come from the
// current or the previous engine block. Python never runs on the engine
// thread, and nothing here is called from it.
#include "components.hpp"

#include <pybind11/native_enum.h>
#include <pybind11/stl.h>

#include <string>
#include <vector>

#include "../support/overrides.hpp"
#include "../values/values.hpp"
#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace bindings {

using rack::engine::Input;
using rack::engine::Light;
using rack::engine::LightInfo;
using rack::engine::Output;
using rack::engine::Param;
using rack::engine::Port;
using rack::engine::PortInfo;
using rack::engine::PORT_MAX_CHANNELS;

namespace {

struct InputRef : PortRef {};
struct OutputRef : PortRef {};

/** A checked view of a port's voltage array from `first` to the end. */
struct VoltagesRef {
	PortRef port;
	int first = 0;
	int size() const { return PORT_MAX_CHANNELS - first; }
	int index(int i) const {
		int n = size();
		if (i < 0)
			i += n;
		if (i < 0 || i >= n)
			throw py::index_error("voltage index out of range (a port has " + std::to_string(PORT_MAX_CHANNELS) + " channels)");
		return first + i;
	}
};

uint8_t channel(int c) {
	if (c < 0 || c >= PORT_MAX_CHANNELS)
		throw py::index_error("channel must be 0 to " + std::to_string(PORT_MAX_CHANNELS - 1) + ", not " + std::to_string(c));
	return (uint8_t) c;
}

uint8_t channelCount(int n) {
	if (n < 0 || n > PORT_MAX_CHANNELS)
		throw py::value_error("channels must be 0 to " + std::to_string(PORT_MAX_CHANNELS) + ", not " + std::to_string(n));
	return (uint8_t) n;
}

template <typename T, typename Ref>
Ref owned(const char* type) {
	auto storage = std::make_shared<T>();
	Ref ref;
	ref.ptr = storage.get();
	ref.storage = storage;
	ref.type = type;
	return ref;
}

template <typename Ref, typename T>
py::object borrow(T* component, std::shared_ptr<runtime::ObjectToken> owner, const char* type) {
	uint64_t generation = owner ? owner->generation : 0;
	return runtime::identity(component, generation, [&] {
		Ref ref;
		ref.ptr = component;
		ref.owner = std::move(owner);
		ref.type = type;
		return py::cast(ref);
	});
}

/** Members of an owned component are keyed with generation 0: the wrapper keeps the storage, and so the address, alive. */
template <typename U, typename T>
py::object memberView(const ComponentRef<T>& parent, U* member, const char* type) {
	uint64_t generation = parent.owner ? parent.owner->generation : 0;
	return runtime::identity(member, generation, [&] { return py::cast(parent.member(member, type)); });
}

std::string portRepr(const PortRef& r, const char* cls) {
	Port& p = r.get();
	return std::string(cls) + "(channels=" + std::to_string(p.getChannels()) + ", voltage=" +
		values::floatRepr(p.getVoltage(0)) + (r.owned() ? ", owned" : "") + ")";
}

/** Python overrides of the info classes' virtual names (callback strategy "ui"). */
template <typename Base>
struct InfoTrampoline : Base, py::trampoline_self_life_support {
	using Base::Base;
	std::string getName() override {
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_name", [this] { return Base::getName(); });
	}
	std::string getDescription() override {
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_description", [this] { return Base::getDescription(); });
	}
};

}  // namespace

py::object borrowInput(Input* port, std::shared_ptr<runtime::ObjectToken> owner) {
	return borrow<InputRef>(static_cast<Port*>(port), std::move(owner), "input");
}

py::object borrowOutput(Output* port, std::shared_ptr<runtime::ObjectToken> owner) {
	return borrow<OutputRef>(static_cast<Port*>(port), std::move(owner), "output");
}

py::object borrowParam(Param* param, std::shared_ptr<runtime::ObjectToken> owner) {
	return borrow<ParamRef>(param, std::move(owner), "param");
}

py::object borrowLight(Light* light, std::shared_ptr<runtime::ObjectToken> owner) {
	return borrow<LightRef>(light, std::move(owner), "light");
}

void bindComponents(py::module_ m) {
	m.attr("PORT_MAX_CHANNELS") = PORT_MAX_CHANNELS;

	// --- Light (Port uses it for plug lights) -----------------------------------------
	py::class_<LightRef>(m, "Light", "rack::engine::Light: a light's brightness (the square of its brightness, by convention).")
		.def(py::init([] { return owned<Light, LightRef>("light"); }))
		.def_property("value", [](const LightRef& l) { return l.get().value; }, [](const LightRef& l, float v) { l.get().value = v; },
			"Unstable API in Rack: prefer get_brightness and set_brightness.")
		.def("set_brightness", [](const LightRef& l, float b) { l.get().setBrightness(b); }, py::arg("brightness"))
		.def("get_brightness", [](const LightRef& l) { return l.get().getBrightness(); })
		// The int overload is Rack's deprecated per-frame form; it comes first so an int reaches it, as in C++.
		.def("set_brightness_smooth", [](const LightRef& l, float b, int frames) {
			deprecated("Light.set_brightness_smooth(brightness, frames) is deprecated in Rack; pass delta_time instead");
			RACKPY_CALL_DEPRECATED(l.get().setBrightnessSmooth(b, frames);)
		}, py::arg("brightness"), py::arg("frames") = 1)
		.def("set_brightness_smooth", [](const LightRef& l, float b, float deltaTime, float lambda) {
			l.get().setBrightnessSmooth(b, deltaTime, lambda);
		}, py::arg("brightness"), py::arg("delta_time"), py::arg("lambda_") = 30.f,
			"Slow fall, immediate rise; lambda_ is the decay rate (Rack's default is about 2 screen frames).")
		.def("set_smooth_brightness", [](const LightRef& l, float b, float deltaTime) {
			deprecated("Light.set_smooth_brightness is deprecated in Rack; use set_brightness_smooth");
			l.get().setSmoothBrightness(b, deltaTime);
		}, py::arg("brightness"), py::arg("delta_time"))
		.def_property("brightness", [](const LightRef& l) { return l.get().getBrightness(); },
			[](const LightRef& l, float b) { l.get().setBrightness(b); }, "Facade: get_brightness and set_brightness.")
		.def_property_readonly("alive", &LightRef::alive)
		.def("__repr__", [](const LightRef& l) { return "Light(brightness=" + values::floatRepr(l.get().getBrightness()) + ")"; });

	// --- Param --------------------------------------------------------------------------
	py::class_<ParamRef>(m, "Param", "rack::engine::Param: a parameter's value.")
		.def(py::init([] { return owned<Param, ParamRef>("param"); }))
		.def_property("value", [](const ParamRef& p) { return p.get().value; }, [](const ParamRef& p, float v) { p.get().value = v; },
			"Unstable API in Rack: prefer get_value and set_value.")
		.def("get_value", [](const ParamRef& p) { return p.get().getValue(); })
		.def("set_value", [](const ParamRef& p, float v) { p.get().setValue(v); }, py::arg("value"))
		.def_property_readonly("alive", &ParamRef::alive)
		.def("__repr__", [](const ParamRef& p) { return "Param(value=" + values::floatRepr(p.get().getValue()) + ")"; });

	// --- Port, Input, Output -----------------------------------------------------------
	py::class_<VoltagesRef>(m, "Voltages", py::buffer_protocol(),
		"A port's voltage array (Port::voltages). Indexing is checked and reads or writes the port. A zero-copy "
		"buffer (memoryview) is available only for a port RackPy owns; use snapshot() for a Rack-owned one.")
		.def("__len__", &VoltagesRef::size)
		.def("__getitem__", [](const VoltagesRef& v, int i) { return v.port.get().voltages[v.index(i)]; })
		.def("__setitem__", [](const VoltagesRef& v, int i, float x) { v.port.get().voltages[v.index(i)] = x; })
		.def("__iter__", [](const VoltagesRef& v) {
			Port& p = v.port.get();
			return py::iter(py::cast(std::vector<float>(p.voltages + v.first, p.voltages + PORT_MAX_CHANNELS)));
		})
		.def("tolist", [](const VoltagesRef& v) {
			Port& p = v.port.get();
			return std::vector<float>(p.voltages + v.first, p.voltages + PORT_MAX_CHANNELS);
		})
		.def("snapshot", [](const VoltagesRef& v) {
			Port& p = v.port.get();
			py::object array = py::module_::import("array").attr("array")("f");
			array.attr("extend")(std::vector<float>(p.voltages + v.first, p.voltages + PORT_MAX_CHANNELS));
			return array;
		}, "An independent copy, as array.array('f').")
		.def_buffer([](VoltagesRef& v) -> py::buffer_info {
			if (!v.port.owned())
				throw py::buffer_error("a memoryview of a Rack-owned port could outlive its module; use snapshot()");
			Port& p = v.port.get();
			return py::buffer_info(p.voltages + v.first, (py::ssize_t) v.size());
		});

	py::class_<PortRef> port(m, "Port", "rack::engine::Port: up to PORT_MAX_CHANNELS polyphonic voltages.");
	py::native_enum<Port::Type>(port, "Type", "enum.Enum", "Port::Type")
		.value("INPUT", Port::INPUT)
		.value("OUTPUT", Port::OUTPUT)
		.finalize();
	port.def(py::init([] { return owned<Port, PortRef>("port"); }))
		.def("set_voltage", [](const PortRef& r, float v, int c) { r.get().setVoltage(v, channel(c)); }, py::arg("voltage"),
			py::arg("channel") = 0)
		.def("get_voltage", [](const PortRef& r, int c) { return r.get().getVoltage(channel(c)); }, py::arg("channel") = 0)
		.def("get_poly_voltage", [](const PortRef& r, int c) { return r.get().getPolyVoltage(channel(c)); }, py::arg("channel"),
			"The channel's voltage if polyphonic, else channel 0's.")
		.def("get_normal_voltage", [](const PortRef& r, float n, int c) { return r.get().getNormalVoltage(n, channel(c)); },
			py::arg("normal_voltage"), py::arg("channel") = 0, "The voltage if connected, else normal_voltage.")
		.def("get_normal_poly_voltage", [](const PortRef& r, float n, int c) { return r.get().getNormalPolyVoltage(n, channel(c)); },
			py::arg("normal_voltage"), py::arg("channel"))
		.def("get_voltages", [](const PortRef& r, int first) {
			channel(first);
			r.get();
			return VoltagesRef{r, first};
		}, py::arg("first_channel") = 0, "A checked view of the voltages from first_channel on (Rack returns a float*).")
		.def("read_voltages", [](const PortRef& r) {
			Port& p = r.get();
			std::vector<float> out((size_t) p.getChannels());
			p.readVoltages(out.data());
			return out;
		}, "A copy of the first `channels` voltages (Rack copies into a caller's array).")
		.def("write_voltages", [](const PortRef& r, const std::vector<float>& v) {
			Port& p = r.get();
			if (v.size() < (size_t) p.getChannels())
				throw py::value_error("write_voltages needs at least " + std::to_string(p.getChannels()) + " values (the port's channels)");
			p.writeVoltages(v.data());
		}, py::arg("voltages"), "Copies the first `channels` values in; set the channel count first, as in Rack.")
		.def("clear_voltages", [](const PortRef& r) { r.get().clearVoltages(); })
		.def("get_voltage_sum", [](const PortRef& r) { return r.get().getVoltageSum(); })
		.def("get_voltage_rms", [](const PortRef& r) { return r.get().getVoltageRMS(); })
		.def("set_channels", [](const PortRef& r, int n) { r.get().setChannels(channelCount(n)); }, py::arg("channels"),
			"Sets the channel count, clearing higher channels. Does nothing on a disconnected port; 0 means 1.")
		.def("get_channels", [](const PortRef& r) { return r.get().getChannels(); })
		.def("is_connected", [](const PortRef& r) { return r.get().isConnected(); })
		.def("is_monophonic", [](const PortRef& r) { return r.get().isMonophonic(); })
		.def("is_polyphonic", [](const PortRef& r) { return r.get().isPolyphonic(); })
		.def("normalize", [](const PortRef& r, float n) {
			deprecated("Port.normalize is deprecated in Rack; use get_normal_voltage");
			float v = 0.f;
			RACKPY_CALL_DEPRECATED(v = r.get().normalize(n);)
			return v;
		}, py::arg("normal_voltage"))
		.def_property_readonly("voltages", [](const PortRef& r) {
			r.get();
			return VoltagesRef{r, 0};
		}, "Unstable API in Rack: the voltage array as a checked view. Prefer get_voltage and set_voltage.")
		.def_property("value", [](const PortRef& r) {
			deprecated("Port.value is deprecated in Rack; use get_voltage");
			return r.get().value;
		}, [](const PortRef& r, float v) {
			deprecated("Port.value is deprecated in Rack; use set_voltage");
			r.get().value = v;
		}, "Deprecated and unstable in Rack: channel 0's voltage (a union with voltages).")
		.def_property("channels", [](const PortRef& r) {
			deprecated("Port.channels is deprecated in Rack; use get_channels");
			return (int) r.get().channels;
		}, [](const PortRef& r, int n) {
			deprecated("Port.channels is deprecated in Rack; use set_channels");
			r.get().channels = channelCount(n);
		}, "Deprecated and unstable in Rack: the raw channel count, written without set_channels' rules.")
		.def_property("active", [](const PortRef& r) {
			deprecated("Port.active is deprecated in Rack; use is_connected");
			return (int) r.get().active;
		}, [](const PortRef& r, int n) {
			deprecated("Port.active is deprecated in Rack");
			r.get().active = channelCount(n);
		}, "Deprecated and unstable in Rack: a union with channels.")
		.def_property_readonly("plug_lights", [](const PortRef& r) {
			Port& p = r.get();
			return py::make_tuple(memberView(r, &p.plugLights[0], "plug light"), memberView(r, &p.plugLights[1], "plug light"),
				memberView(r, &p.plugLights[2], "plug light"));
		}, "The three lights on a cable's plug (positive, negative, polyphonic), as views of this port's lights.")
		// Facade conveniences (rackpy.engine).
		.def_property("voltage", [](const PortRef& r) { return r.get().getVoltage(0); },
			[](const PortRef& r, float v) { r.get().setVoltage(v, 0); }, "Facade: channel 0's voltage.")
		.def_property("channel_count", [](const PortRef& r) { return r.get().getChannels(); },
			[](const PortRef& r, int n) { r.get().setChannels(channelCount(n)); }, "Facade: get_channels and set_channels.")
		.def_property_readonly("connected", [](const PortRef& r) { return r.get().isConnected(); })
		.def_property_readonly("monophonic", [](const PortRef& r) { return r.get().isMonophonic(); })
		.def_property_readonly("polyphonic", [](const PortRef& r) { return r.get().isPolyphonic(); })
		.def_property_readonly("alive", &PortRef::alive)
		.def("__repr__", [](const PortRef& r) { return portRepr(r, "Port"); });

	py::class_<InputRef, PortRef>(m, "Input", "rack::engine::Input: a module's input Port.")
		.def(py::init([] { return owned<Input, InputRef>("input"); }))
		.def("__repr__", [](const InputRef& r) { return portRepr(r, "Input"); });
	py::class_<OutputRef, PortRef>(m, "Output", "rack::engine::Output: a module's output Port.")
		.def(py::init([] { return owned<Output, OutputRef>("output"); }))
		.def("__repr__", [](const OutputRef& r) { return portRepr(r, "Output"); });

	// --- PortInfo and LightInfo -------------------------------------------------------
	py::classh<PortInfo, InfoTrampoline<PortInfo>>(m, "PortInfo", "rack::engine::PortInfo: a port's name and description.")
		.def(py::init<>())
		.def_readwrite("type", &PortInfo::type)
		.def_readwrite("port_id", &PortInfo::portId)
		.def_readwrite("name", &PortInfo::name)
		.def_readwrite("description", &PortInfo::description)
		.def("get_name", &PortInfo::getName)
		.def("get_full_name", &PortInfo::getFullName, "get_name() with \" input\" or \" output\" appended.")
		.def("get_description", &PortInfo::getDescription);
	py::classh<LightInfo, InfoTrampoline<LightInfo>>(m, "LightInfo", "rack::engine::LightInfo: a light's name and description.")
		.def(py::init<>())
		.def_readwrite("light_id", &LightInfo::lightId)
		.def_readwrite("name", &LightInfo::name)
		.def_readwrite("description", &LightInfo::description)
		.def("get_name", &LightInfo::getName)
		.def("get_description", &LightInfo::getDescription);
}

}  // namespace bindings
}  // namespace rackpy
