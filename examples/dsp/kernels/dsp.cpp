// SPDX-License-Identifier: GPL-3.0-or-later
// The DSP example's kernel library (RP-36): a polyphonic biquad filter built from Rack's
// own DSP library: rack::dsp::TBiquadFilter<float_4>, four channels at a time, with its
// coefficients recomputed every 32 frames by a rack::dsp::ClockDivider, as Rack's own
// modules do to keep the cost of the trigonometry down. Python uses it as
// rackpy.dsp.kernel.load("dsp.Biquad"). Original code, written from the pinned headers.
#include <rack.hpp>
#include <rackpy/kernel.hpp>

#include <algorithm>
#include <cmath>

namespace {

using rack::simd::float_4;
using Type = rack::dsp::TBiquadFilter<float_4>::Type;

/** The filter types the MODE switch chooses, in its order. */
constexpr Type TYPES[] = {Type::LOWPASS, Type::HIGHPASS, Type::BANDPASS, Type::NOTCH};
constexpr int UPDATE_FRAMES = 32;

struct Biquad : rackpy::Kernel {
	int cutoff, resonance, mode, input, output;
	rack::dsp::TBiquadFilter<float_4> filters[rack::PORT_MAX_CHANNELS / 4];
	rack::dsp::ClockDivider update;
	bool fresh = true;  // compute the coefficients on the next frame, whatever the divider says

	explicit Biquad(const std::vector<double>& v)
		: cutoff((int) v[0]), resonance((int) v[1]), mode((int) v[2]), input((int) v[3]), output((int) v[4]) {
		update.setDivision(UPDATE_FRAMES);
	}

	/** CUTOFF is octaves above 20 Hz; RESONANCE is the filter's Q. */
	static float hertz(float octaves) { return 20.f * std::pow(2.f, octaves); }

	void configure(rack::engine::Module& m, float sampleRate) {
		float f = std::min(hertz(m.params[cutoff].getValue()) / sampleRate, 0.45f);  // below Nyquist
		float q = m.params[resonance].getValue();
		int index = rack::math::clamp((int) std::round(m.params[mode].getValue()), 0, 3);
		for (auto& filter : filters)
			filter.setParameters(TYPES[index], f, q, 1.f);
	}

	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {
		if (update.process() || fresh) {
			configure(m, args.sampleRate);
			fresh = false;
		}
		rack::engine::Input& in = m.inputs[input];
		rack::engine::Output& out = m.outputs[output];
		int channels = std::max(1, in.getChannels());
		for (int c = 0; c < channels; c += 4)
			out.setVoltageSimd(filters[c / 4].process(in.getPolyVoltageSimd<float_4>(c)), c);
		out.setChannels(channels);
		return true;
	}

	void reset() noexcept override {
		for (auto& filter : filters)
			filter.reset();
		update.reset();
		fresh = true;
	}

	void sampleRateChange(float, float) noexcept override { fresh = true; }
};

}  // namespace

RACKPY_KERNEL_LIBRARY("dsp", registrar) {
	registrar.add(rackpy::KernelType{
		"Biquad",
		"A polyphonic biquad filter: Rack's TBiquadFilter<float_4>, coefficients updated every 32 frames.",
		{
			{"cutoff", rackpy::Field::PARAM, 0, true, "octaves above 20 Hz"},
			{"resonance", rackpy::Field::PARAM, 1, true, "the filter's Q"},
			{"mode", rackpy::Field::PARAM, 2, true, "0 lowpass, 1 highpass, 2 bandpass, 3 notch"},
			{"input", rackpy::Field::INPUT, 0, true, "the signal, one channel per voice"},
			{"output", rackpy::Field::OUTPUT, 0, true, "the filtered signal, as many channels"},
		},
		{},
		[](const std::vector<double>& v) { return std::make_unique<Biquad>(v); },
	});
}
