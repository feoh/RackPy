// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

#include <rack.hpp>

namespace rackpy {
namespace bindings {

/** rackpy.raw.audio (RP-29B). */
void bindAudio(pybind11::module_ audio);
/** A live audio port Python made (rackpy.raw.audio.Port), for Rack's audio widgets and menus. Main thread. */
rack::audio::Port& pythonAudioPort(pybind11::handle value);

}  // namespace bindings
}  // namespace rackpy
