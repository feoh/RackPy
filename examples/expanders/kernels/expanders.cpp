// SPDX-License-Identifier: GPL-3.0-or-later
// The expanders example's kernel library (RP-36): the Peeker, which reads the module on
// its left directly, as a C++ module reads a neighbour through Rack's leftExpander, after
// checking that the neighbour really is this plugin's Source. Python uses it as
// rackpy.dsp.kernel.load("expanders.Peek"). Original code, written from the pinned Rack
// headers.
//
// Reading a neighbour's params and ports from process() is Rack's own expander pattern.
// Rack may process the two modules in either order, or at once on two engine threads, so
// what Peek reads is the neighbour's value from this frame or the frame before. When
// timing matters, exchange messages instead (the Source and the Receiver do): a message
// always arrives exactly one frame later.
#include <rack.hpp>
#include <rackpy/kernel.hpp>

#include <algorithm>
#include <cstring>

namespace {

struct Peek : rackpy::Kernel {
	int levelOut, signalOut, seenLight;
	int sourceParam, sourceOutput;

	explicit Peek(const std::vector<double>& v)
		: levelOut((int) v[0]), signalOut((int) v[1]), seenLight((int) v[2]), sourceParam((int) v[3]),
		  sourceOutput((int) v[4]) {}

	/** The left neighbour, if it is a Source of this module's own plugin with the expected params and ports. */
	rack::engine::Module* source(rack::engine::Module& m) const noexcept {
		rack::engine::Module* left = m.leftExpander.module;
		if (!left || !left->model || !m.model || left->model->plugin != m.model->plugin)
			return nullptr;
		if (std::strcmp(left->model->slug.c_str(), "Source") != 0)
			return nullptr;
		if (sourceParam >= (int) left->params.size() || sourceOutput >= (int) left->outputs.size())
			return nullptr;
		return left;
	}

	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs&) noexcept override {
		rack::engine::Module* left = source(m);
		rack::engine::Output& level = m.outputs[levelOut];
		rack::engine::Output& signal = m.outputs[signalOut];
		if (!left) {
			level.setVoltage(0.f);
			signal.setChannels(1);
			signal.setVoltage(0.f);
			m.lights[seenLight].setBrightness(0.f);
			return true;
		}
		// The Source's LEVEL knob (0 to 1) as 0 to 10 V, and its output, every channel.
		level.setVoltage(10.f * left->params[sourceParam].getValue());
		rack::engine::Output& theirs = left->outputs[sourceOutput];
		int channels = std::max(1, theirs.getChannels());
		for (int c = 0; c < channels; c++)
			signal.setVoltage(theirs.getVoltage(c), c);
		signal.setChannels(channels);
		m.lights[seenLight].setBrightness(1.f);
		return true;
	}
};

}  // namespace

RACKPY_KERNEL_LIBRARY("expanders", registrar) {
	registrar.add(rackpy::KernelType{
		"Peek",
		"Reads the Source on the module's left directly: its level knob and its output.",
		{
			{"level", rackpy::Field::OUTPUT, 0, true, "the Source's LEVEL as 0 to 10 V"},
			{"signal", rackpy::Field::OUTPUT, 1, true, "a copy of the Source's output"},
			{"seen", rackpy::Field::LIGHT, 0, true, "on while a Source is on the left"},
			{"source_param", rackpy::Field::NUMBER, 0, false, "the Source's LEVEL param"},
			{"source_output", rackpy::Field::NUMBER, 0, false, "the Source's output port"},
		},
		{},
		[](const std::vector<double>& v) { return std::make_unique<Peek>(v); },
	});
}
