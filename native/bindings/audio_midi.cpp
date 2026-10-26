// SPDX-License-Identifier: GPL-3.0-or-later
// Family 7 adapter: the generated registry calls bind_audio_midi(). rack::midi
// becomes rackpy.raw.midi (native/bindings/midi.cpp) and rack::audio
// rackpy.raw.audio (native/bindings/audio.cpp).
#include "midi.hpp"

namespace rackpy {
namespace bindings {

void bind_audio_midi(pybind11::module_ raw) {
	bindMidi(raw.def_submodule("midi", "rack::midi: MIDI messages, drivers, devices and ports."));
}

}  // namespace bindings
}  // namespace rackpy
