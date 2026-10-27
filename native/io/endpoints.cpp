// SPDX-License-Identifier: GPL-3.0-or-later
// A RackPy module's I/O endpoints (RP-31J): see endpoints.hpp.
#include "endpoints.hpp"

#include <algorithm>

namespace rackpy {
namespace io {

Endpoints::Endpoints() {
	publish();
}

void Endpoints::publish() {
	frame = Frame{};
	frame.audioInput = audioIn;
	frame.audioOutput = audioOut;
	frame.audioInputs = audio ? audio->inputs : 0;
	frame.audioOutputs = audio ? audio->outputs : 0;
	active = audio != nullptr;
	for (int k = 0; k < MAX_MIDI_INPUTS; k++)
		active = active || midi[k] != nullptr;
}

void Endpoints::detach() noexcept {
	detachAudioPort();
	for (int k = 0; k < MAX_MIDI_INPUTS; k++)
		detachMidiInput(k);
}

void Endpoints::detachMidiInput(int index) noexcept {
	if (index < 0 || index >= MAX_MIDI_INPUTS)
		return;
	if (detachMidi[index]) {
		auto undo = std::move(detachMidi[index]);
		detachMidi[index] = nullptr;
		undo();
	}
	midi[index].reset();
	publish();
}

void Endpoints::detachAudioPort() noexcept {
	if (detachAudio) {
		auto undo = std::move(detachAudio);
		detachAudio = nullptr;
		undo();  // never throws (the bindings make it noexcept-safe)
	}
	audio.reset();
	publish();
}

void Endpoints::beginFrame(int64_t at) noexcept {
	if (audio)
		audio->shift(audioIn);
	std::fill(std::begin(audioOut), std::end(audioOut), 0.f);
	for (int k = 0; k < MAX_MIDI_INPUTS; k++) {
		if (MidiQueue* q = midi[k].get()) {
			q->collect(at);
			frame.midi[k] = q->due();
			frame.midiCount[k] = q->dueCount();
		}
	}
}

void Endpoints::endFrame() noexcept {
	if (audio)
		audio->push(audioOut);
}

void Endpoints::sampleRateChanged() noexcept {
	if (audio)
		audio->sampleRateChanged();
}

namespace {

/** What kernel libraries call: the cross-cast in RackPy's own code. */
struct HostServices final : Services {
	const Frame* frame(rack::engine::Module& self, Link& link) noexcept override { return frameOf(self, link); }
};

}  // namespace

Services& hostServices() {
	static HostServices services;
	return services;
}

}  // namespace io
}  // namespace rackpy
