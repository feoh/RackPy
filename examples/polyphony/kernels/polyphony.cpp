// SPDX-License-Identifier: GPL-3.0-or-later
// The polyphony example's kernel library (RP-36): a polyphonic sine oscillator, written
// twice as a C++ plugin author would, once a channel at a time and once four channels at
// a time with Rack's SIMD types. Rack's engine runs either every frame; no Python runs
// there. Python uses them as rackpy.dsp.kernel.load("polyphony.Scalar") and
// load("polyphony.Simd"). Original code, written from the pinned Rack headers.
#include <rack.hpp>
#include <rackpy/kernel.hpp>

#include <algorithm>
#include <cmath>

namespace {

using rack::simd::float_4;

/** The fields both types take: which param, input and output, and the amplitude. */
struct Wiring {
	int pitch, voct, sine;
	float amplitude;
	explicit Wiring(const std::vector<double>& v)
		: pitch((int) v[0]), voct((int) v[1]), sine((int) v[2]), amplitude((float) v[3]) {}
};

/** One voice's state; the oscillator keeps one per channel. */
struct Voice {
	float phase = 0.f;  // in cycles, [0, 1)

	float process(float volts, float sampleTime) {
		float freq = rack::dsp::FREQ_C4 * std::pow(2.f, volts);
		phase += freq * sampleTime;
		phase -= std::floor(phase);
		return std::sin(2.f * float(M_PI) * phase);
	}
};

/** A channel at a time. As many channels as the V/OCT cable has, and at least one. */
struct Scalar : rackpy::Kernel, Wiring {
	Voice voices[rack::PORT_MAX_CHANNELS];

	using Wiring::Wiring;

	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {
		rack::engine::Input& in = m.inputs[voct];
		rack::engine::Output& out = m.outputs[sine];
		float knob = m.params[pitch].getValue();
		int channels = std::max(1, in.getChannels());
		for (int c = 0; c < channels; c++)
			out.setVoltage(amplitude * voices[c].process(knob + in.getPolyVoltage(c), args.sampleTime), c);
		out.setChannels(channels);
		return true;
	}

	void reset() noexcept override {
		for (Voice& v : voices)
			v = Voice();
	}

	void setControl(int, double value) noexcept override { amplitude = (float) value; }
};

/** Four channels at a time: each float_4 holds four voices' phases. */
struct Simd : rackpy::Kernel, Wiring {
	float_4 phases[rack::PORT_MAX_CHANNELS / 4] = {};

	using Wiring::Wiring;

	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {
		rack::engine::Input& in = m.inputs[voct];
		rack::engine::Output& out = m.outputs[sine];
		float knob = m.params[pitch].getValue();
		int channels = std::max(1, in.getChannels());
		for (int c = 0; c < channels; c += 4) {
			float_4 volts = knob + in.getPolyVoltageSimd<float_4>(c);
			float_4 freq = rack::dsp::FREQ_C4 * rack::simd::pow(2.f, volts);
			float_4& phase = phases[c / 4];
			phase += freq * args.sampleTime;
			phase -= rack::simd::floor(phase);
			// Channels past the last are computed too and ignored: the output has `channels` channels.
			out.setVoltageSimd(amplitude * rack::simd::sin(2.f * float(M_PI) * phase), c);
		}
		out.setChannels(channels);
		return true;
	}

	void reset() noexcept override {
		for (float_4& p : phases)
			p = 0.f;
	}

	void setControl(int, double value) noexcept override { amplitude = (float) value; }
};

std::vector<rackpy::Field> fields() {
	return {
		{"pitch", rackpy::Field::PARAM, 0, true, "the pitch knob, in volts from middle C"},
		{"voct", rackpy::Field::INPUT, 0, true, "the 1 V/octave input; its channels are the voices"},
		{"sine", rackpy::Field::OUTPUT, 0, true, "the sine output, one channel per voice"},
		{"amplitude", rackpy::Field::NUMBER, 5, false, "the peak voltage (a control)"},
	};
}

}  // namespace

RACKPY_KERNEL_LIBRARY("polyphony", registrar) {
	registrar.add(rackpy::KernelType{
		"Scalar",
		"A polyphonic sine oscillator, one channel at a time.",
		fields(),
		{"amplitude"},
		[](const std::vector<double>& v) { return std::make_unique<Scalar>(v); },
	});
	registrar.add(rackpy::KernelType{
		"Simd",
		"A polyphonic sine oscillator, four channels at a time with float_4.",
		fields(),
		{"amplitude"},
		[](const std::vector<double>& v) { return std::make_unique<Simd>(v); },
	});
}
