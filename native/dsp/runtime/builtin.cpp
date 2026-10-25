// SPDX-License-Identifier: GPL-3.0-or-later
// RackPy's built-in kernels (RP-17A). Their arithmetic is the RP-07
// reference's (benchmarks/spike/src/dsp.hpp), so they can be compared with
// direct C++ sample for sample.
#include <array>
#include <cmath>

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
}

}  // namespace dsp
}  // namespace rackpy
