// SPDX-License-Identifier: GPL-3.0-or-later
// An example RackPy kernel library (RP-17B): a wavefolder. Built with the
// Rack SDK's plugin flags, listed in rackpy-loader.json as
// "kernels": ["kernels/kernels_example.so"], and used from Python as
// rackpy.dsp.kernel.load("example.Fold").
#include <rack.hpp>
#include <rackpy/kernel.hpp>

#include <cmath>

namespace {

/** Folds each channel of the input back into [-limit, limit], after a gain. */
struct Fold : rackpy::Kernel {
	int input, output;
	float gain, limit;

	explicit Fold(const std::vector<double>& v)
		: input((int) v[0]), output((int) v[1]), gain((float) v[2]), limit((float) v[3]) {
		if (limit <= 0.f)
			throw std::invalid_argument("example.Fold: limit must be positive");
	}

	static float fold(float x, float limit) {
		float period = 4.f * limit;
		float t = std::fmod(x + limit, period);
		if (t < 0.f)
			t += period;
		return t < 2.f * limit ? t - limit : 3.f * limit - t;
	}

	// Runs on Rack's engine thread every frame: no allocation, locks, IO or exceptions.
	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs&) noexcept override {
		rack::engine::Input& in = m.inputs[input];
		rack::engine::Output& out = m.outputs[output];
		int channels = in.getChannels();
		out.setChannels(channels);
		for (int c = 0; c < channels; c++)
			out.setVoltage(fold(in.getVoltage(c) * gain, limit), c);
		return true;
	}

	void setControl(int slot, double value) noexcept override {
		if (slot == 0)
			gain = (float) value;
		else if (slot == 1 && value > 0)
			limit = (float) value;
	}
};

}  // namespace

RACKPY_KERNEL_LIBRARY("example", registrar) {
	registrar.add(rackpy::KernelType{
		"Fold",
		"Folds each input channel back into [-limit, limit] after a gain.",
		{
			{"input", rackpy::Field::INPUT, 0, true, "the input port"},
			{"output", rackpy::Field::OUTPUT, 0, true, "the output port"},
			{"gain", rackpy::Field::NUMBER, 1, false, "applied before folding (a control)"},
			{"limit", rackpy::Field::NUMBER, 5, false, "the fold threshold in volts (a control)"},
		},
		{"gain", "limit"},
		[](const std::vector<double>& v) { return std::make_unique<Fold>(v); },
	});
}
