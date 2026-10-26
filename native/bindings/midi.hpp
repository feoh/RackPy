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
/** `device` is going: every port pointing at it points nowhere. Main thread. */
void disconnectOutputDevice(rack::midi::OutputDevice& device) noexcept;

}  // namespace bindings
}  // namespace rackpy
