// SPDX-License-Identifier: GPL-3.0-or-later
// RackPy's built-in kernels (RP-17A). Their arithmetic is the RP-07
// reference's (benchmarks/spike/src/dsp.hpp), so they can be compared with
// direct C++ sample for sample. rackpy.AudioInterface and rackpy.MidiToCV
// (RP-31J) are Core Audio's and Core MIDI-CV's per-frame processing
// (src/core/Audio.cpp, MIDI_CV.cpp), around the module's audio port and MIDI
// inputs.
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "../../io/endpoints.hpp"
#include "rackpy/graph.hpp"
#include "runtime.hpp"

namespace rackpy {
namespace dsp {

namespace {

int index(double value) {
	return (int) value;
}

/** out = in × gain × (param, if wired), per channel. */
struct Gain : Kernel {
	int input, output, param;
	float gain;
	Gain(const std::vector<double>& v) : input(index(v[0])), output(index(v[1])), param(index(v[2])), gain((float) v[3]) {}
	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {
		rack::engine::Input& in = m.inputs[input];
		rack::engine::Output& out = m.outputs[output];
		float g = param >= 0 ? m.params[param].getValue() * gain : gain;
		int channels = in.getChannels();
		out.setChannels(channels);
		for (int c = 0; c < channels; c++)
			out.setVoltage(in.getVoltage(c) * g, c);
		return true;
	}
	void setControl(int slot, double value) noexcept override {
		if (slot == 0)
			gain = (float) value;
	}
};

/** A sine oscillator per channel of the pitch input (V/oct around C4), at least one. */
struct Sine : Kernel {
	int pitchInput, output, pitchParam;
	float amplitude;
	std::array<float, rack::engine::PORT_MAX_CHANNELS> phase{};
	Sine(const std::vector<double>& v)
		: pitchInput(index(v[0])), output(index(v[1])), pitchParam(index(v[2])), amplitude((float) v[3]) {}
	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {
		int channels = pitchInput >= 0 ? std::max(1, m.inputs[pitchInput].getChannels()) : 1;
		float offset = pitchParam >= 0 ? m.params[pitchParam].getValue() : 0.f;
		rack::engine::Output& out = m.outputs[output];
		out.setChannels(channels);
		for (int c = 0; c < channels; c++) {
			float pitch = (pitchInput >= 0 ? m.inputs[pitchInput].getPolyVoltage(c) : 0.f) + offset;
			float freq = rack::dsp::FREQ_C4 * std::pow(2.f, pitch);
			phase[c] += freq * args.sampleTime;
			if (phase[c] >= 1.f)
				phase[c] -= 1.f;
			out.setVoltage(amplitude * std::sin(2.f * float(M_PI) * phase[c]), c);
		}
		return true;
	}
	void reset() noexcept override {
		phase.fill(0.f);
	}
	void setControl(int slot, double value) noexcept override {
		if (slot == 0)
			amplitude = (float) value;
	}
};

/** Core Audio's process(): device inputs to outputs at 10 V full scale, inputs' voltage sums to the device at a tenth. */
struct AudioInterface : Kernel {
	int input, output, channels;
	io::Link link;
	AudioInterface(const std::vector<double>& v) : input(index(v[0])), output(index(v[1])), channels(count(v[2])) {}
	static int count(double value) {
		if (!(value >= 1 && value <= io::MAX_AUDIO_CHANNELS) || value != std::floor(value))
			throw std::invalid_argument("rackpy.AudioInterface.channels must be a whole number from 1 to " +
				std::to_string(io::MAX_AUDIO_CHANNELS));
		return (int) value;
	}
	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {
		const io::Frame* f = io::frameOf(m, link);
		if (!f)
			return true;
		int n = std::min(channels, io::MAX_AUDIO_CHANNELS);
		for (int c = 0; c < n; c++) {
			// Audio.cpp:261-268: an input's voltage sum, a tenth; 0 when unpatched.
			if (input >= 0 && (size_t) (input + c) < m.inputs.size() && c < f->audioOutputs)
				f->audioOutput[c] = m.inputs[(size_t) (input + c)].isConnected()
					? m.inputs[(size_t) (input + c)].getVoltageSum() / 10.f : 0.f;
			// Audio.cpp:316-322 and 330-333: the device's sample times 10, or 0 V.
			if (output >= 0 && (size_t) (output + c) < m.outputs.size())
				m.outputs[(size_t) (output + c)].setVoltage(10.f * (c < f->audioInputs ? f->audioInput[c] : 0.f));
		}
		return true;
	}
};

/** A whole number from `low` to `high`, or std::invalid_argument naming `field`. */
int whole(double value, double low, double high, const char* field) {
	if (!(value >= low && value <= high) || value != std::floor(value))
		throw std::invalid_argument(std::string("rackpy.MidiToCV.") + field + " must be a whole number from " +
			std::to_string((long long) low) + " to " + std::to_string((long long) high));
	return (int) value;
}

/** Core MIDI-CV's process(): one MIDI input's messages through Rack's MidiParser to its 12 outputs, in its order. */
struct MidiToCV : Kernel {
	graph::MidiCV cv;
	int output;
	io::Link link;
	MidiToCV(const std::vector<double>& v) : output(index(v[1])) {
		cv.input = whole(v[0], 0, io::MAX_MIDI_INPUTS - 1, "midi_input");
		configure(v);
	}
	std::vector<double> settings;
	void configure(const std::vector<double>& v) {
		settings = v;
		cv.configure(whole(v[2], 1, 16, "channels"), whole(v[3], 0, 3, "mono_mode"), whole(v[4], 0, 3, "poly_mode"),
			(float) v[5], v[6] != 0, (uint32_t) whole(v[7], 1, 1 << 20, "clock_division"), v[8] != 0, v[9] != 0);
	}
	void set(rack::engine::Module& m, int k, int channels, float value, int c) {
		if (output < 0 || (size_t) (output + k) >= m.outputs.size())
			return;
		rack::engine::Output& o = m.outputs[(size_t) (output + k)];
		if (c == 0 && channels > 0)
			o.setChannels(channels);
		o.setVoltage(value, c);
	}
	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {
		// MIDI_CV.cpp:60-102.
		const io::Frame* f = io::frameOf(m, link);
		auto& p = cv.parser;
		if (f)
			for (int i = 0; i < f->midiCount[cv.input]; i++) {
				io::toMessage(f->midi[cv.input][i], cv.message);
				p.processMessage(cv.message);
			}
		p.processFilters(args.sampleTime);
		for (uint8_t c = 0; c < p.channels; c++) {
			set(m, graph::MIDI_PITCH, p.channels, p.getPitchVoltage(c), c);
			set(m, graph::MIDI_GATE, p.channels, p.gates[c] ? 10.f : 0.f, c);
			set(m, graph::MIDI_VELOCITY, p.channels, p.velocities[c] / 127.f * 10.f, c);
			set(m, graph::MIDI_AFTERTOUCH, p.channels, p.aftertouches[c] / 127.f * 10.f, c);
			set(m, graph::MIDI_RETRIGGER, p.channels, p.retriggerPulses[c].isHigh() ? 10.f : 0.f, c);
		}
		uint8_t wheels = p.getWheelChannels();
		for (uint8_t c = 0; c < wheels; c++) {
			set(m, graph::MIDI_PITCH_WHEEL, wheels, p.getPw(c) * 5.f, c);
			set(m, graph::MIDI_MOD_WHEEL, wheels, p.getMod(c) * 10.f, c);
		}
		set(m, graph::MIDI_CLOCK, 0, p.clockPulse.isHigh() ? 10.f : 0.f, 0);
		set(m, graph::MIDI_CLOCK_DIVIDER, 0, p.clockDividerPulse.isHigh() ? 10.f : 0.f, 0);
		set(m, graph::MIDI_START, 0, p.startPulse.isHigh() ? 10.f : 0.f, 0);
		set(m, graph::MIDI_STOP, 0, p.stopPulse.isHigh() ? 10.f : 0.f, 0);
		set(m, graph::MIDI_CONTINUE, 0, p.continuePulse.isHigh() ? 10.f : 0.f, 0);
		p.processPulses(args.sampleTime);
		return true;
	}
	void reset() noexcept override { cv.reset(); }
};

}  // namespace

void registerBuiltins() {
	if (findType("rackpy.Gain"))
		return;
	registerType(KernelType{
		"rackpy.Gain",
		"Multiplies each channel of an input by a gain, and by a parameter's value if one is wired.",
		{
			{"input", Field::INPUT, 0, true, "the input port"},
			{"output", Field::OUTPUT, 0, true, "the output port, with the input's channel count"},
			{"param", Field::PARAM, -1, false, "a parameter to multiply by, or -1"},
			{"gain", Field::NUMBER, 1, false, "a constant factor (a control)"},
		},
		{"gain"},
		[](const std::vector<double>& v) { return std::make_unique<Gain>(v); },
	});
	registerType(KernelType{
		"rackpy.Sine",
		"A sine oscillator for each channel of a V/oct pitch input (0 V is C4), at least one channel.",
		{
			{"pitch_input", Field::INPUT, -1, false, "a V/oct input, or -1 for 0 V"},
			{"output", Field::OUTPUT, 0, true, "the output port"},
			{"pitch_param", Field::PARAM, -1, false, "a parameter added to the pitch in volts, or -1"},
			{"amplitude", Field::NUMBER, 5, false, "the peak voltage (a control)"},
		},
		{"amplitude"},
		[](const std::vector<double>& v) { return std::make_unique<Sine>(v); },
	});
	registerType(KernelType{
		"rackpy.AudioInterface",
		"Core Audio's processing around the module's audio port (Module.set_audio_port): each device input to an "
		"output at 10 V full scale, each input's voltage sum to a device output at a tenth.",
		{
			{"input", Field::INPUT, -1, false, "the first input sent to the device (channel 0), or -1 for none"},
			{"output", Field::OUTPUT, -1, false, "the first output the device's inputs reach (channel 0), or -1"},
			{"channels", Field::NUMBER, 2, false, "how many consecutive inputs and outputs, 1 to 16"},
		},
		{},
		[](const std::vector<double>& v) { return std::make_unique<AudioInterface>(v); },
	});
	registerType(KernelType{
		"rackpy.MidiToCV",
		"Core MIDI-CV's processing of one of the module's MIDI inputs (Module.set_midi_input): Rack's MidiParser, fed "
		"each message at its frame, to 12 consecutive outputs in MIDI-CV's order (pitch, gate, velocity, aftertouch, "
		"pitch wheel, mod wheel, retrigger, clock, clock divider, start, stop, continue).",
		{
			{"midi_input", Field::NUMBER, 0, false, "which of the module's MIDI inputs, 0 to 3"},
			{"output", Field::OUTPUT, 0, true, "the first of the 12 outputs (any beyond the module's are skipped)"},
			{"channels", Field::NUMBER, 1, false, "polyphony channels, 1 to 16 (MIDI-CV's menu)"},
			{"mono_mode", Field::NUMBER, 0, false, "0 last, 1 first, 2 lowest, 3 highest note"},
			{"poly_mode", Field::NUMBER, 0, false, "0 rotate, 1 reuse, 2 reset, 3 MPE"},
			{"pitch_bend_range", Field::NUMBER, 2, false, "semitones"},
			{"smooth", Field::NUMBER, 1, false, "1 to smooth the pitch and mod wheels"},
			{"clock_division", Field::NUMBER, 24, false, "24 PPQN clocks per CLK/N pulse"},
			{"retrigger_on_resume", Field::NUMBER, 0, false, "1 to retrigger on legato release"},
			{"release_velocity", Field::NUMBER, 0, false, "1 for release velocity"},
		},
		{},
		[](const std::vector<double>& v) { return std::make_unique<MidiToCV>(v); },
	});
}

}  // namespace dsp
}  // namespace rackpy
