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

}  // namespace bindings
}  // namespace rackpy
