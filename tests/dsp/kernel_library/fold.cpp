// SPDX-License-Identifier: GPL-3.0-or-later
// An example RackPy kernel library (RP-17B): a wavefolder. Built with the
// Rack SDK's plugin flags, listed in rackpy-loader.json as
// "kernels": ["kernels/kernels_example.so"], and used from Python as
// rackpy.dsp.kernel.load("example.Fold"). It also adds graph node types
// (RP-21C): Graph.node("example.Fold", ...) and Graph.node("example.Hold", ...).
#include <rack.hpp>
#include <rackpy/graph.hpp>
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

/** The same fold as a graph node: each channel of its input, times `drive`, folded into [-limit, limit]. */
struct FoldNode : rackpy::graph::Node {
	float drive, limit;
	FoldNode(float drive, float limit) : drive(drive), limit(limit) {}
	void process(rackpy::graph::Signal& out, const rackpy::graph::Signal* const* in,
		const rack::engine::Module::ProcessArgs&) noexcept override {
		rackpy::graph::map1(out, *in[0], [this](float x) { return Fold::fold(x * drive, limit); });
	}
};

/** Sample and hold: each channel of input 0, taken when the same channel of input 1 rises to 1 V. */
struct Hold : rackpy::graph::Node {
	rack::dsp::SchmittTrigger trigger[rackpy::graph::MAX_CHANNELS];
	rackpy::graph::Signal held;
	void process(rackpy::graph::Signal& out, const rackpy::graph::Signal* const* in,
		const rack::engine::Module::ProcessArgs&) noexcept override {
		int n = std::max(in[0]->channels, in[1]->channels);
		held.channels = n;
		for (int c = 0; c < n; c++)
			if (trigger[c].process(rackpy::graph::lane(*in[1], c)))
				held.v[c] = rackpy::graph::lane(*in[0], c);
		out = held;
	}
	void reset() noexcept override {
		held = rackpy::graph::Signal();
		for (auto& t : trigger)
			t.reset();
	}
};

}  // namespace

RACKPY_GRAPH_NODES(registrar) {
	registrar.add(rackpy::graph::NodeType{
		"Fold",
		"Each channel of its input, times drive, folded back into [-limit, limit].",
		1,
		{"drive", "limit"},
		[](const std::vector<double>& a) -> std::unique_ptr<rackpy::graph::Node> {
			if (a[1] <= 0)
				throw std::invalid_argument("example.Fold: limit must be positive");
			return std::make_unique<FoldNode>((float) a[0], (float) a[1]);
		},
	});
	registrar.add(rackpy::graph::NodeType{
		"Hold",
		"Sample and hold: input 0, taken when input 1 rises to 1 V (Rack's SchmittTrigger).",
		2,
		{},
		[](const std::vector<double>&) { return std::unique_ptr<rackpy::graph::Node>(new Hold); },
	});
}

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
