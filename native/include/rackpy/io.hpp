// SPDX-License-Identifier: GPL-3.0-or-later
// RackPy audio-port and MIDI-input I/O for native code (RP-31J): what a
// module's kernel or graph receives from its audio port and MIDI inputs on
// Rack's engine thread, and what it sends to the audio device.
//
// Python gives a RackPy module an audio port (Module.set_audio_port) and up
// to MAX_MIDI_INPUTS MIDI input queues (Module.set_midi_input) on the main
// thread. From then on RackPy's native code moves the port's driver buffers
// and the queues' messages through lock-free rings, as Rack's Core Audio and
// MIDI-CV modules buffer them, and once per frame, before the module's kernel
// runs, publishes a Frame: one frame of the device's input, a frame of device
// output for the kernel to fill, and the MIDI messages due in this frame.
//
// A kernel library is a separate DSO with its own C++ runtime, so RTTI never
// crosses into it: RackPy finds a module's Frame in its own code and hands
// the library a Services table when it loads it, if the library defines
// RACKPY_IO. Without them frame() is null, and the module has no I/O.
//
// Include it after <rack.hpp>; it uses only Rack's public API.
#pragma once
#include <rack.hpp>

#include <cstdint>

namespace rackpy {
namespace io {

/** Bumped whenever MidiMessage, Frame, Link, Services or RACKPY_IO change incompatibly. */
constexpr int IO_ABI = 1;

/** The most channels a module's audio port carries each way: Rack's PORT_MAX_CHANNELS, as Core's Audio-16. */
constexpr int MAX_AUDIO_CHANNELS = 16;
/** The most MIDI input queues a module has. */
constexpr int MAX_MIDI_INPUTS = 4;
/** The longest message a MIDI input carries natively: longer ones (SysEx) are dropped and counted. */
constexpr int MAX_MIDI_BYTES = 3;

/** One MIDI message: Rack's midi::Message without its heap-allocated byte vector. */
struct MidiMessage {
	int64_t frame = -1;
	uint8_t size = 0;
	uint8_t bytes[MAX_MIDI_BYTES] = {};
};

/** What a module's I/O gives its kernel in one frame, on Rack's engine thread, during Kernel::process. */
struct Frame {
	/** The device's input samples for this frame, MAX_AUDIO_CHANNELS of them, in the device's units (full scale
	is 1). 0 for channels the device does not have and for frames that have not arrived, as Core Audio's
	outputs read 0 V then. */
	const float* audioInput = nullptr;
	/** The samples to send the device, MAX_AUDIO_CHANNELS of them, zeroed before each frame. RackPy sends the
	first channels the device has after the kernel returns. */
	float* audioOutput = nullptr;
	/** The port's configured input and output channels (0 without an audio port). */
	int audioInputs = 0;
	int audioOutputs = 0;
	/** Each MIDI input's messages due in this frame (frame at or before ProcessArgs::frame), in frame order and,
	within a frame, in the order they arrived, as Rack's InputQueue::tryPop gives them. Valid for this frame. */
	const MidiMessage* midi[MAX_MIDI_INPUTS] = {};
	int midiCount[MAX_MIDI_INPUTS] = {};
};

/** What RackPy remembers about a module between frames, so that it finds the module's Frame only when the module
changes. Keep one per kernel, and leave its fields to RackPy. */
struct Link {
	const void* module = nullptr;
	int64_t id = -1;
	void* endpoints = nullptr;
};

/** Copies `m` into Rack's message type. With `out.bytes` holding its default 3 bytes, this never allocates. */
inline void toMessage(const MidiMessage& m, rack::midi::Message& out) noexcept {
	out.bytes.resize(m.size);
	for (int i = 0; i < m.size; i++)
		out.bytes[(size_t) i] = m.bytes[i];
	out.frame = m.frame;
}

/** RackPy's I/O services, as a kernel library receives them. */
struct Services {
	/** `self`'s Frame for the current frame, or null if `self` is not a RackPy module. Engine thread, during
	Kernel::process. */
	virtual const Frame* frame(rack::engine::Module& self, Link& link) noexcept = 0;

protected:
	~Services() = default;
};

/** The services RackPy gave this library when it loaded it, or null. */
inline Services*& services() {
	static Services* given = nullptr;
	return given;
}

inline const Frame* frame(rack::engine::Module& self, Link& link) noexcept {
	Services* s = services();
	return s ? s->frame(self, link) : nullptr;
}

}  // namespace io
}  // namespace rackpy

/** Defines a kernel library's I/O entry points, once, next to its RACKPY_KERNEL_LIBRARY: RackPy then gives the
library its services before any of its kernels is made.

	RACKPY_IO
*/
#define RACKPY_IO \
	extern "C" __attribute__((visibility("default"))) int rackpy_io_abi() { return rackpy::io::IO_ABI; } \
	extern "C" __attribute__((visibility("default"))) void rackpy_set_io_services(rackpy::io::Services* given) { \
		rackpy::io::services() = given; \
	}
