// SPDX-License-Identifier: GPL-3.0-or-later
// RP-07 real-host benchmark plugin. Native modules and RackPy-shaped modules
// (command queue + native kernel, no Python on the audio path) run in actual
// Rack. Each logs its processed frame count, Rack's engine meter and its last
// output when deleted. Spike code, not RackPy.
#include "dsp.hpp"

#include <memory>
#include <string>

using namespace rack;
using namespace bench;

Plugin* pluginInstance;

namespace {

struct Stats {
	std::atomic<uint64_t> processed{0};
	// Last output while the input was cabled. Rack removes cables before modules
	// at shutdown, so the final output alone says nothing about the feedback loop.
	std::atomic<float> connectedOut{0.f};
	std::atomic<int> maxInputChannels{0};
	std::string name;
};

void report(engine::Module* m, Stats& s) {
	INFO("RACKPY_BENCH model=%s id=%lld processed=%llu meter_avg=%.6f meter_max=%.6f connected_out=%.6f "
		"max_input_channels=%d cables=%zu sample_rate=%g",
		s.name.c_str(), (long long) m->id, (unsigned long long) s.processed.load(), APP->engine->getMeterAverage(),
		APP->engine->getMeterMax(), s.connectedOut.load(), s.maxInputChannels.load(), APP->engine->getNumCables(),
		APP->engine->getSampleRate());
}

// --- kernels used by the RackPy-shaped modules --------------------------------------

struct Kernel {
	virtual ~Kernel() = default;
	virtual void process(engine::Module* m, const engine::Module::ProcessArgs& args) = 0;
};

struct SineKernel : Kernel {
	Sine osc;
	void process(engine::Module* m, const engine::Module::ProcessArgs& args) override {
		m->outputs[0].setVoltage(osc.step(m->params[0].getValue() + m->inputs[0].getVoltage(), args.sampleTime));
	}
};

struct PolyKernel : Kernel {
	PolySine osc;
	void process(engine::Module* m, const engine::Module::ProcessArgs& args) override {
		float pitch[16];
		for (int c = 0; c < 16; c++)
			pitch[c] = m->inputs[0].getPolyVoltage(c) + 0.01f * c;
		float out[16];
		osc.step(pitch, out, 16, args.sampleTime);
		m->outputs[0].setChannels(16);
		m->outputs[0].writeVoltages(out);
	}
};

struct FeedbackKernel : Kernel {
	void process(engine::Module* m, const engine::Module::ProcessArgs& args) override {
		m->outputs[0].setVoltage(m->inputs[0].getVoltage() + 1.f);
	}
};

template <typename K>
struct RackPyShaped : engine::Module {
	Stats stats;
	SpscRing<Command, 256> commands;
	float mirror[16] = {};
	std::unique_ptr<Kernel> kernel{new K};
	RackPyShaped() {
		config(1, 1, 1, 0);
		configParam(0, -2.f, 2.f, 0.f);
	}
	~RackPyShaped() override { report(this, stats); }
	void process(const ProcessArgs& args) override {
		Command c;
		while (commands.pop(c))
			mirror[c.slot & 15] = c.value;
		kernel->process(this, args);
		if (inputs[0].isConnected())
			stats.connectedOut.store(outputs[0].getVoltage(), std::memory_order_relaxed);
		if (inputs[0].getChannels() > stats.maxInputChannels.load(std::memory_order_relaxed))
			stats.maxInputChannels.store(inputs[0].getChannels(), std::memory_order_relaxed);
		stats.processed.fetch_add(1, std::memory_order_relaxed);
	}
};

// --- native modules -----------------------------------------------------------------

struct NativeSine : engine::Module {
	Stats stats;
	Sine osc;
	NativeSine() {
		config(1, 1, 1, 0);
		configParam(0, -2.f, 2.f, 0.f);
	}
	~NativeSine() override { report(this, stats); }
	void process(const ProcessArgs& args) override {
		outputs[0].setVoltage(osc.step(params[0].getValue() + inputs[0].getVoltage(), args.sampleTime));
		stats.processed.fetch_add(1, std::memory_order_relaxed);
	}
};

struct NativePoly : engine::Module {
	Stats stats;
	PolySine osc;
	NativePoly() { config(1, 1, 1, 0); }
	~NativePoly() override { report(this, stats); }
	void process(const ProcessArgs& args) override {
		float pitch[16];
		for (int c = 0; c < 16; c++)
			pitch[c] = inputs[0].getPolyVoltage(c) + 0.01f * c;
		float out[16];
		osc.step(pitch, out, 16, args.sampleTime);
		outputs[0].setChannels(16);
		outputs[0].writeVoltages(out);
		stats.processed.fetch_add(1, std::memory_order_relaxed);
	}
};

struct NativeFeedback : engine::Module {
	Stats stats;
	NativeFeedback() { config(1, 1, 1, 0); }
	~NativeFeedback() override { report(this, stats); }
	void process(const ProcessArgs& args) override {
		outputs[0].setVoltage(inputs[0].getVoltage() + 1.f);
		if (inputs[0].isConnected())
			stats.connectedOut.store(outputs[0].getVoltage(), std::memory_order_relaxed);
		if (inputs[0].getChannels() > stats.maxInputChannels.load(std::memory_order_relaxed))
			stats.maxInputChannels.store(inputs[0].getChannels(), std::memory_order_relaxed);
		stats.processed.fetch_add(1, std::memory_order_relaxed);
	}
};

template <typename M>
struct BenchWidget : app::ModuleWidget {
	BenchWidget(M* module) {
		setModule(module);
		box.size = math::Vec(app::RACK_GRID_WIDTH * 2, app::RACK_GRID_HEIGHT);
		// Rack silently drops a patch cable whose ends have no PortWidget.
		addInput(createInputCentered<componentlibrary::PJ301MPort>(math::Vec(15, 100), module, 0));
		addOutput(createOutputCentered<componentlibrary::PJ301MPort>(math::Vec(15, 200), module, 0));
	}
};

#define BENCH_MODEL(Type, Slug) \
	struct Type##Named : Type { \
		Type##Named() { stats.name = Slug; } \
	}; \
	p->addModel(createModel<Type##Named, BenchWidget<Type##Named>>(Slug));

}  // namespace

extern "C" void init(Plugin* p) {
	pluginInstance = p;
	using RackPySine = RackPyShaped<SineKernel>;
	using RackPyPoly = RackPyShaped<PolyKernel>;
	using RackPyFeedback = RackPyShaped<FeedbackKernel>;
	BENCH_MODEL(NativeSine, "NativeSine")
	BENCH_MODEL(RackPySine, "RackPySine")
	BENCH_MODEL(NativePoly, "NativePoly")
	BENCH_MODEL(RackPyPoly, "RackPyPoly")
	BENCH_MODEL(NativeFeedback, "NativeFeedback")
	BENCH_MODEL(RackPyFeedback, "RackPyFeedback")
}
