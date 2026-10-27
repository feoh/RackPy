// SPDX-License-Identifier: GPL-3.0-or-later
// RP-17A: the kernel path under the RP-07 audio-path guard. Run with
// LD_PRELOAD=audio_guard.so: every engine-thread call (process, with control
// draining, kernel pickup, retirement and failure) is marked, and the guard
// counts heap, lock, write and GIL calls inside the marks. Main-thread work
// (attach, set, reclaim) stays outside them, as in Rack. Prints JSON.
// RP-21B adds a graph run: a 16-channel plan with every node type, its
// controls set through the queue and the plan replaced while it runs; RP-21C
// the same for a compiled graph kernel (tests/dsp/kernel_library/graphs.cpp),
// alternating single-channel and 16-channel input to cover both its paths.
// RP-23C adds expander exchange: an interpreted and a compiled pair sending
// and receiving lanes every frame, with snapshots on, and Rack's flips.
// RP-31J adds a module's audio port: the driver thread's half of Core Audio's
// buffering (as master, stepping the module's frames itself, and as a
// secondary), resampling with converters the main thread prepares and swaps,
// a rate change's missed block, stream starts and stops, and the module's
// frames around rackpy.AudioInterface. Only the main thread's preparation
// and Rack's own engine calls (not made here) stay outside the marks.
#include "../dsp/graph/plan.hpp"
#include "../engine/expanders.hpp"
#include "../module/module.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace rackpy;

// The compiled tier's kernel library, linked in: its registration function.
extern "C" void rackpy_register_kernels(rackpy::KernelRegistrar& registrar);

namespace {

struct Compiled : KernelRegistrar {
	std::vector<std::unique_ptr<KernelType>> types;
	void add(KernelType type) override { types.push_back(std::make_unique<KernelType>(std::move(type))); }
	std::unique_ptr<dsp::Hosted> make(const std::string& name) {
		for (auto& type : types)
			if (type->name == name) {
				auto h = std::make_unique<dsp::Hosted>();
				h->type = type.get();
				for (const Field& f : type->fields)
					h->values.push_back(f.defaultValue);
				h->kernel = type->create(h->values);
				return h;
			}
		std::abort();
	}
};

using GuardFn = void (*)();
using ReportFn = void (*)(char*, size_t);
GuardFn enter, leave, resetCounts;
ReportFn report;

std::unique_ptr<dsp::Hosted> make(const char* name, std::vector<double> values) {
	auto hosted = std::make_unique<dsp::Hosted>();
	hosted->type = dsp::findType(name);
	hosted->kernel = hosted->type->create(values);
	return hosted;
}

/** A deliberate violation the guard must see: a heap allocation it cannot optimize away. */
struct Allocating : Kernel {
	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs&) noexcept override {
		void* volatile p = std::malloc(16);
		m.outputs[0].setVoltage(p ? 1.f : 0.f);
		std::free(p);
		return true;
	}
};

/** Fails at once: the failure path (silence and a diagnostic) must be clean too. */
struct Failing : Kernel {
	bool process(rack::engine::Module&, const rack::engine::Module::ProcessArgs&) noexcept override { return false; }
};

std::string run(module::Module& m, int frames, bool swapping) {
	resetCounts();
	rack::engine::Module::ProcessArgs args;
	args.sampleRate = 48000.f;
	args.sampleTime = 1.f / 48000.f;
	for (int n = 0; n < frames; n++) {
		args.frame = n;
		if (swapping && n % 1000 == 500)
			m.kernels.attach(make("rackpy.Gain", {0, 0, 0, 1}), true);
		if (swapping && n % 1000 == 900)
			m.kernels.attach(make("rackpy.Sine", {0, 1, 0, 5}), true);
		if (n % 7 == 0 && m.kernels.latest())
			m.kernels.set(m.kernels.latest(), 0, 0.5 + (n % 3), true);
		m.inputs[0].setVoltage((float) (n % 11) / 5.f - 1.f);
		enter();
		m.process(args);
		leave();
		if (n % 100 == 0)
			m.kernels.reclaim();
	}
	char text[512];
	report(text, sizeof text);
	return text;
}

/** A plan with every node type, on 16 channels; `seed` varies its constants so replacements differ. */
std::unique_ptr<dsp::Hosted> everyNode(double seed) {
	graph::PlanSpec spec;
	auto node = [&](const char* op, std::vector<long long> inputs = {}, std::vector<double> args = {}) {
		spec.nodes.push_back({op, std::move(inputs), std::move(args), {}});
		return (long long) spec.nodes.size() - 1;
	};
	long long in = node("input", {}, {0}), param = node("param", {}, {0});
	long long level = node("control", {}, {seed});
	spec.controls.push_back({"level", level, seed});
	long long rate = node("sample_rate"), time = node("sample_time");
	long long pitch = node("add", {in, param});
	long long voices = node("sin2pi", {node("phasor", {node("pitch_to_freq", {pitch})})});
	long long mixed = node("mul", {voices, level});
	long long shaped = node("clamp", {node("sub", {mixed, node("div", {in, node("constant", {}, {3})})}),
		node("neg", {node("abs", {level})}), level});
	long long picked = node("where", {node("gt", {in, param}), node("min", {shaped, rate}), node("max", {shaped, time})});
	long long edge = node("schmitt", {in}, {0.1, 1.0});
	long long gate = node("pulse", {edge}, {0.001});
	long long held = node("delay", {}, {0});
	spec.latches.emplace_back(held, node("add", {node("mul", {held, node("constant", {}, {0.5})}), picked}));
	node("output", {node("add", {held, gate})}, {0});
	node("output", {node("lt", {node("le", {edge, gate}), node("ge", {in, param})})}, {1});
	auto type = graph::planType(spec);
	auto hosted = std::make_unique<dsp::Hosted>();
	hosted->type = type.get();
	hosted->ownedType = type;
	hosted->values = graph::planDefaults(*type);
	hosted->kernel = type->create(hosted->values);
	return hosted;
}

std::string runGraph(module::Module& m, int frames) {
	resetCounts();
	rack::engine::Module::ProcessArgs args;
	args.sampleRate = 48000.f;
	args.sampleTime = 1.f / 48000.f;
	for (int n = 0; n < frames; n++) {
		args.frame = n;
		if (n % 1000 == 500)
			m.kernels.attach(everyNode(1 + n % 7), true);
		if (n % 7 == 0)
			m.kernels.set(m.kernels.latest(), 0, 0.5 + (n % 3), true);
		for (int c = 0; c < 16; c++)
			m.inputs[0].setVoltage((float) ((n + c) % 11) / 5.f - 1.f, c);
		enter();
		m.process(args);
		leave();
		if (n % 100 == 0)
			m.kernels.reclaim();
	}
	char text[512];
	report(text, sizeof text);
	return text;
}

std::string runCompiled(module::Module& m, Compiled& compiled, int frames) {
	resetCounts();
	rack::engine::Module::ProcessArgs args;
	args.sampleRate = 48000.f;
	args.sampleTime = 1.f / 48000.f;
	for (int n = 0; n < frames; n++) {
		args.frame = n;
		if (n % 1000 == 500)
			m.kernels.attach(compiled.make("Everything"), true);
		if (n % 7 == 0)
			m.kernels.set(m.kernels.latest(), 0, 0.5 + (n % 3), true);
		int channels = (n / 250) % 2 ? 16 : 1;  // both of the compiled kernel's paths
		m.inputs[0].channels = m.inputs[1].channels = (uint8_t) channels;
		for (int c = 0; c < channels; c++) {
			m.inputs[0].setVoltage((float) ((n + c) % 11) / 5.f - 1.f, c);
			m.inputs[1].setVoltage((float) ((n + 2 * c) % 7) / 3.f - 1.f, c);
		}
		enter();
		m.process(args);
		leave();
		if (n % 100 == 0)
			m.kernels.reclaim();
	}
	char text[512];
	report(text, sizeof text);
	return text;
}

/** Gives `side` of `m` two "rackpy.graph" buffers of `lanes` lanes, as Python's graph_messages() does. */
void giveMessages(module::Module& m, uint8_t side, size_t lanes) {
	auto a = engine::allocateMessage(engine::GRAPH_SCHEMA, lanes * graph::LANE);
	auto b = engine::allocateMessage(engine::GRAPH_SCHEMA, lanes * graph::LANE);
	m.getExpander(side).producerMessage = a->data;
	m.getExpander(side).consumerMessage = b->data;
	m.expanderMessages.push_back(a);
	m.expanderMessages.push_back(b);
	engine::publishChannel(m, m, side);
	m.snapshotsWanted[side].store(true);
}

std::unique_ptr<dsp::Hosted> plan(const graph::PlanSpec& spec) {
	auto type = graph::planType(spec);
	auto hosted = std::make_unique<dsp::Hosted>();
	hosted->type = type.get();
	hosted->ownedType = type;
	hosted->values = graph::planDefaults(*type);
	hosted->kernel = type->create(hosted->values);
	return hosted;
}

/** The interpreted twins of graphs.Sender and graphs.Echo (tests/dsp/in_rack_libraries/graphs.py). */
graph::PlanSpec senderPlan() {
	graph::PlanSpec spec;
	auto node = [&](const char* op, std::vector<long long> inputs = {}, std::vector<double> args = {}) {
		spec.nodes.push_back({op, std::move(inputs), std::move(args), {}});
		return (long long) spec.nodes.size() - 1;
	};
	long long count = node("delay", {}, {1});
	spec.latches.emplace_back(count, node("add", {count, node("constant", {}, {1})}));
	node("expander_send", {node("input", {}, {0})}, {1, 0});
	node("expander_send", {count}, {1, 1});
	node("output", {node("expander_receive", {}, {1, 0})}, {0});
	return spec;
}

graph::PlanSpec echoPlan() {
	graph::PlanSpec spec;
	auto node = [&](const char* op, std::vector<long long> inputs = {}, std::vector<double> args = {}) {
		spec.nodes.push_back({op, std::move(inputs), std::move(args), {}});
		return (long long) spec.nodes.size() - 1;
	};
	long long got = node("expander_receive", {}, {0, 0});
	node("output", {got}, {0});
	node("output", {node("expander_receive", {}, {0, 1})}, {1});
	node("expander_send", {node("mul", {got, node("constant", {}, {2})})}, {0, 0});
	return spec;
}

void place(rack::engine::Module& left, rack::engine::Module& right) {
	left.rightExpander.moduleId = right.id;
	left.rightExpander.module = &right;
	right.leftExpander.moduleId = left.id;
	right.leftExpander.module = &left;
}

bool exchanged = false;

std::string runExpanders(Compiled& compiled, int frames) {
	module::Module a, b, c, d;  // a -> b interpreted, c -> d compiled
	module::Module* all[] = {&a, &b, &c, &d};
	for (int k = 0; k < 4; k++) {
		all[k]->id = 100 + k;
		all[k]->config(0, k % 2 ? 0 : 1, k % 2 ? 2 : 1, 0);
		for (auto& o : all[k]->outputs)
			o.channels = 1;
	}
	a.inputs[0].channels = c.inputs[0].channels = 16;
	giveMessages(a, 1, 1);
	giveMessages(b, 0, 2);
	giveMessages(c, 1, 1);
	giveMessages(d, 0, 2);
	place(a, b);
	place(c, d);
	a.kernels.attach(plan(senderPlan()), true);
	b.kernels.attach(plan(echoPlan()), true);
	c.kernels.attach(compiled.make("Sender"), true);
	d.kernels.attach(compiled.make("Echo"), true);
	resetCounts();
	rack::engine::Module::ProcessArgs args;
	args.sampleRate = 48000.f;
	args.sampleTime = 1.f / 48000.f;
	for (int n = 0; n < frames; n++) {
		args.frame = n;
		for (int ch = 0; ch < 16; ch++) {
			a.inputs[0].setVoltage((float) ((n + ch) % 11) / 5.f - 1.f, ch);
			c.inputs[0].setVoltage((float) ((n + ch) % 11) / 5.f - 1.f, ch);
		}
		enter();
		for (module::Module* m : all)
			m->process(args);
		for (module::Module* m : all)  // Engine_stepFrame's flips
			for (rack::engine::Module::Expander* e : {&m->leftExpander, &m->rightExpander})
				if (e->messageFlipRequested) {
					std::swap(e->producerMessage, e->consumerMessage);
					e->messageFlipRequested = false;
				}
		leave();
		if (n % 100 == 0)
			for (module::Module* m : all)
				m->kernels.reclaim();
	}
	char text[512];
	report(text, sizeof text);
	// The pairs did exchange: the last frame received the counter of the frame before, in both tiers alike.
	exchanged = b.outputs[1].getVoltage() == (float) (frames - 1) && d.outputs[1].getVoltage() == (float) (frames - 1) &&
		a.outputs[0].getVoltage() == c.outputs[0].getVoltage() && b.outputs[0].getChannels() == 16 &&
		d.outputs[0].getChannels() == 16;
	return text;
}

bool audioFlowed = false;

std::string runAudio(int blocks) {
	module::Module m;
	m.config(0, 2, 2, 0);
	m.inputs[0].channels = m.inputs[1].channels = 1;
	m.outputs[0].channels = m.outputs[1].channels = 1;
	m.audio = std::make_shared<io::AudioBridge>(2, 2);
	m.publish();
	m.kernels.attach(make("rackpy.AudioInterface", {0, 0, 2}), true);
	io::AudioBridge& bridge = *m.audio;
	const int block = 256;
	std::vector<float> in(2 * block), out(2 * block);
	float deviceRate = 44100.f, engineRate = 48000.f;
	bridge.prepare(deviceRate, engineRate, 2, 2);  // as Module.set_audio_port does
	resetCounts();
	rack::engine::Module::ProcessArgs args;
	args.sampleRate = engineRate;
	args.sampleTime = 1.f / engineRate;
	int64_t frame = 0;
	float peak = 0.f;
	for (int b = 0; b < blocks; b++) {
		if (b == blocks / 3)
			deviceRate = 48000.f;  // the device's rate changes: equal rates, Rack's copy
		if (b == 2 * blocks / 3)
			deviceRate = 96000.f;  // and again: a missed block, then the converter the main thread prepares
		bool master = b % 50 < 40;  // and a while as a secondary, stepped in another master's blocks
		for (int i = 0; i < 2 * block; i++)
			in[(size_t) i] = (float) ((b + i) % 13) / 13.f - 0.5f;
		enter();
		if (b % 97 == 5)
			bridge.startStream();
		int requested = bridge.input(in.data(), 2, block, master, engineRate, deviceRate, 2, 2);
		int steps = master ? requested : 300;
		for (int n = 0; n < steps; n++) {
			args.frame = frame++;
			m.inputs[0].setVoltage(m.outputs[0].getVoltage());  // a cable from output to input, one frame late
			m.inputs[1].setVoltage(m.outputs[1].getVoltage());
			m.process(args);
		}
		bridge.output(out.data(), 2, block, engineRate);
		if (b % 97 == 90)
			bridge.stopStream();
		leave();
		for (float x : out)
			peak = std::max(peak, std::fabs(x));
		if (b % 10 == 0)
			bridge.maintain(deviceRate, engineRate, 2, 2);  // a UI step
	}
	char text[512];
	report(text, sizeof text);
	audioFlowed = peak > 0.f && bridge.stats.resamplerMisses.load() > 0;
	return text;
}

}  // namespace

int main() {
	enter = (GuardFn) dlsym(RTLD_DEFAULT, "audio_guard_enter");
	leave = (GuardFn) dlsym(RTLD_DEFAULT, "audio_guard_leave");
	resetCounts = (GuardFn) dlsym(RTLD_DEFAULT, "audio_guard_reset");
	report = (ReportFn) dlsym(RTLD_DEFAULT, "audio_guard_report");
	if (!enter || !leave || !resetCounts || !report) {
		std::fprintf(stderr, "rackpy_kernel_guard needs LD_PRELOAD=audio_guard.so\n");
		return 2;
	}
	runtime::markMainThread();
	dsp::registerBuiltins();
	const int frames = 100000;
	module::Module m;
	m.config(1, 1, 2, 0);
	// Connected ports, as Rack marks them: 16 input channels, mono outputs.
	m.inputs[0].channels = 16;
	m.outputs[0].channels = m.outputs[1].channels = 1;
	m.kernels.attach(make("rackpy.Sine", {0, 1, 0, 5}), true);
	std::string production = run(m, frames, true);
	module::Module failing;
	failing.config(0, 1, 1, 0);
	failing.outputs[0].channels = 1;
	auto hostedFailing = std::make_unique<dsp::Hosted>();
	hostedFailing->kernel = std::make_unique<Failing>();
	failing.kernels.attach(std::move(hostedFailing), true);
	std::string failure = run(failing, frames, false);
	module::Module injected;
	injected.config(0, 1, 1, 0);
	injected.outputs[0].channels = 1;
	auto hostedAllocating = std::make_unique<dsp::Hosted>();
	hostedAllocating->kernel = std::make_unique<Allocating>();
	injected.kernels.attach(std::move(hostedAllocating), true);
	std::string inject = run(injected, frames, false);
	module::Module graphed;
	graphed.config(1, 1, 2, 0);
	graphed.inputs[0].channels = 16;
	graphed.outputs[0].channels = graphed.outputs[1].channels = 1;
	graphed.kernels.attach(everyNode(1), true);
	std::string graphRun = runGraph(graphed, frames);
	Compiled compiled;
	rackpy_register_kernels(compiled);
	module::Module composed;
	composed.config(1, 2, 6, 1);
	for (auto& o : composed.outputs)
		o.channels = 1;
	composed.kernels.attach(compiled.make("Everything"), true);
	std::string compiledRun = runCompiled(composed, compiled, frames);
	// The compiled library is linked in here, so it shares RackPy's expander services directly.
	expander::services() = &engine::hostServices();
	std::string expanderRun = runExpanders(compiled, frames);
	std::string audioRun = runAudio(600);
	std::printf("{\"frames\": %d, \"production\": %s, \"failure\": %s, \"inject\": %s, \"graph\": %s, \"compiled\": %s, "
		"\"expanders\": %s, \"exchanged\": %s, \"audio\": %s, \"audio_flowed\": %s}\n",
		frames, production.c_str(), failure.c_str(), inject.c_str(), graphRun.c_str(), compiledRun.c_str(), expanderRun.c_str(),
		exchanged ? "true" : "false", audioRun.c_str(), audioFlowed ? "true" : "false");
	return 0;
}
