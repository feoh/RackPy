// SPDX-License-Identifier: GPL-3.0-or-later
// A RackPy module's I/O endpoints (RP-31J): its audio port's bridge, its MIDI
// inputs' queues, and the Frame (rackpy/io.hpp) its kernel reads and writes on
// Rack's engine thread. Python-free.
//
// RackPy's Module derives from Endpoints, so native code finds them with a
// cross-cast that only succeeds for RackPy modules. Python sets them on the
// main thread only while Rack's engine does not run the module (in its
// constructor callback, or while Python owns it), so the engine thread reads
// them without synchronization; the bridge's other side, the audio driver,
// synchronizes with the main thread through the device's processMutex, and a
// MIDI queue's producers reach it through its port's atomic pointer, the port
// keeping every queue it fed alive until it goes.
#pragma once
#include <rack.hpp>

#include <functional>
#include <memory>

#include "audio_bridge.hpp"
#include "midi_queue.hpp"
#include "rackpy/io.hpp"

namespace rackpy {
namespace io {

struct Endpoints {
	/** What the kernel sees this frame. */
	Frame frame;
	float audioIn[MAX_AUDIO_CHANNELS] = {};
	float audioOut[MAX_AUDIO_CHANNELS] = {};

	/** The module's audio port's bridge, shared with the port (main thread). */
	std::shared_ptr<AudioBridge> audio;
	/** Undoes the port's side of the attachment (main thread; set by the bindings): after it, the driver no longer
	reaches the bridge, and the module is not Rack's master. */
	std::function<void()> detachAudio;
	/** The module's MIDI inputs' queues, shared with their ports (main thread), and how to undo each port's side. */
	std::shared_ptr<MidiQueue> midi[MAX_MIDI_INPUTS];
	std::function<void()> detachMidi[MAX_MIDI_INPUTS];

	/** Whether any endpoint is set: the engine thread's only check on a module without I/O. */
	bool active = false;

	Endpoints();
	Endpoints(const Endpoints&) = delete;
	Endpoints& operator=(const Endpoints&) = delete;

	/** Main thread, while Rack's engine does not run the module: republish `frame` and `active`. */
	void publish();
	/** Main thread: detach every endpoint. The module's destructor calls it first, while the module is whole. */
	void detach() noexcept;
	/** Main thread: detach the audio port. */
	void detachAudioPort() noexcept;
	/** Main thread: detach MIDI input `index`. */
	void detachMidiInput(int index) noexcept;

	// --- the engine thread, around the module's kernel ---
	/** Before the kernel: the device frame in, the output frame zeroed, the MIDI messages due at `frame`. */
	void beginFrame(int64_t frame) noexcept;
	/** After the kernel: the output frame to the device. */
	void endFrame() noexcept;
	/** Core Audio's onSampleRateChange. */
	void sampleRateChanged() noexcept;
};

/** The endpoints of `m` through `link`'s cross-cast, recomputed only when the module pointer or its ID changes
(a deleted module's address can be reused). Engine thread, lock-free and allocation-free. */
inline Endpoints* resolve(Link& link, rack::engine::Module* m) noexcept {
	if (m != link.module || (m && m->id != link.id)) {
		link.module = m;
		link.id = m ? m->id : -1;
		link.endpoints = m ? dynamic_cast<Endpoints*>(m) : nullptr;
	}
	return static_cast<Endpoints*>(link.endpoints);
}

/** The Frame of `m` for the current frame, or null if `m` is not a RackPy module. Engine thread. */
inline const Frame* frameOf(rack::engine::Module& m, Link& link) noexcept {
	Endpoints* e = resolve(link, &m);
	return e ? &e->frame : nullptr;
}

/** RackPy's implementation of rackpy/io.hpp's Services, which kernel libraries receive. */
Services& hostServices();

}  // namespace io
}  // namespace rackpy
