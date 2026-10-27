// SPDX-License-Identifier: GPL-3.0-or-later
// The laboratory MIDI output device (RP-22C): a Python override of
// midi::OutputDevice::sendMessage, which Rack's own midi::Output::sendMessage
// reaches synchronously. It is never one of Rack's drivers' devices, so only
// Python-made ports that Python connects to it can reach it, on the main thread.
#include "laboratory.hpp"
#include "../bindings/midi.hpp"
#include "../bindings/support/overrides.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace laboratory {

namespace {

namespace m = rack::midi;

struct OutputDevice : m::OutputDevice {
	~OutputDevice() override { bindings::disconnectOutputDevice(*this); }
};

struct PyOutputDevice : OutputDevice, py::trampoline_self_life_support {
	void sendMessage(const m::Message& message) override {
		bindings::overrideOrVoid(static_cast<const OutputDevice*>(this), "send_message",
			[this, &message] { OutputDevice::sendMessage(message); }, message);
	}
};

}  // namespace

void bindMidi(py::module_ lab) {
	py::classh<OutputDevice, PyOutputDevice>(lab, "MidiOutputDevice",
		"A MIDI output device whose send_message a Python subclass overrides; connect Python-made output ports to it.")
		.def(py::init([] {
			requireEnabled("creating a laboratory MIDI device");
			runtime::requireMainThread("creating a laboratory MIDI device");
			return std::make_unique<PyOutputDevice>();
		}))
		.def("send_message", [](OutputDevice& d, const m::Message& message) {
			runtime::requireMainThread("using a laboratory MIDI device");
			if (dynamic_cast<PyOutputDevice*>(&d)) d.OutputDevice::sendMessage(message);
			else d.sendMessage(message);
		}, py::arg("message"), "Rack's OutputDevice::sendMessage: does nothing; a subclass receives each message here.")
		.def("connect", [](OutputDevice& d, py::object output) {
			requireEnabled("connecting a laboratory MIDI device");
			bindings::connectOutputDevice(output, d);
		}, py::arg("output"),
			"Point a rackpy.raw.midi.Output you made at this device, instead of a driver's: its send_message then "
			"reaches this device's send_message, with the port's channel applied, as Rack's Output::sendMessage does.")
		.def_property_readonly("connected", [](const OutputDevice& d) { return d.subscribed.size(); },
			"How many ports point at this device.");
}

}  // namespace laboratory
}  // namespace rackpy
