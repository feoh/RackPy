// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

#include <rack.hpp>

namespace rackpy {
namespace bindings {

/** rackpy.raw.midi (RP-29A). */
void bindMidi(pybind11::module_ midi);
/** A live MIDI port Python made (rackpy.raw.midi.Input, InputQueue or Output), for Rack's MIDI widgets. Main thread. */
rack::midi::Port& pythonMidiPort(pybind11::handle value);
/** Point a Python-made Output at `device`, outside Rack's drivers (the callback
laboratory's MIDI device, RP-22C); the port leaves it when the port goes. Main thread. */
void connectOutputDevice(pybind11::handle output, rack::midi::OutputDevice& device);

/** MIDI devices Rack never lists: the callback laboratory's (RP-22C, RP-22F). Only Python's ports reach them, on
the main thread. A Python port leaves every such device natively when the port is released (deleted, or Rack's
context closed). disconnect() and the destructor make every Python port leave the device and point nowhere, natively. */
struct UnlistedMidiInputDevice : rack::midi::InputDevice {
	UnlistedMidiInputDevice();
	~UnlistedMidiInputDevice() override;
	void disconnect() noexcept;
};
struct UnlistedMidiOutputDevice : rack::midi::OutputDevice {
	UnlistedMidiOutputDevice();
	~UnlistedMidiOutputDevice() override;
	void disconnect() noexcept;
};

/** A MIDI driver Rack never lists (the callback laboratory's, RP-22F): when it goes, every Python port using it
leaves its device and points nowhere, natively. Main thread. */
struct UnlistedMidiDriver : rack::midi::Driver {
	~UnlistedMidiDriver() override;
};

/** Whether `port` is a port Python made that is not being released: only such a port may be handed to Python. Main thread. */
bool livePythonMidiPort(const rack::midi::Port* port);
/** The Python object of a live Python-made port (see livePythonMidiPort). Needs the GIL. */
pybind11::object midiPortObject(rack::midi::Port* port);
/** Point a Python-made port at `driver`, as Rack's Port::setDriverId does once it has found a driver
(src/midi.cpp:108-133): leave the old device, take the driver, then its default device, through the port's
virtuals. Rack's setDriverId finds only drivers Rack lists, so this repeats its calls. Main thread. */
void connectMidiDriver(pybind11::handle port, UnlistedMidiDriver& driver);
/** A device's subscribers, as PythonInputDevice.subscribed lists them; `owner` keeps the device alive. Needs the GIL. */
pybind11::list midiSubscribers(pybind11::object owner, rack::midi::InputDevice& device);
pybind11::list midiSubscribers(pybind11::object owner, rack::midi::OutputDevice& device);

/** Mark a Python-made rackpy.raw.midi.Input as the laboratory's (rackpy.laboratory.MidiInput, RP-22F): its
on_message then runs synchronously where Rack calls it on the main thread. The caller checks enablement. */
void markLaboratoryInput(pybind11::handle input);
bool isLaboratoryInput(pybind11::handle input);
/** rackpy.laboratory.MidiInputQueue: Rack's InputQueue whose on_message a Python subclass overrides (RP-22F). */
void bindLaboratoryInputQueue(pybind11::module_ lab);

}  // namespace bindings
}  // namespace rackpy
