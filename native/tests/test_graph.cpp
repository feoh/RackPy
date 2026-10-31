// SPDX-License-Identifier: GPL-3.0-or-later
// RP-21B: graph plans without Python or a Rack host. Each plan is compared
// sample for sample with the same arithmetic written directly in C++ (its
// twin), and the plan type's checks, lifetime, reset and replacement through
// RP-17A's KernelSlot are driven step by step on one thread.
#include "../dsp/graph/plan.hpp"
#include "../module/module.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

using namespace rackpy;

namespace {

int failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

template <typename F>
bool throwsInvalid(F action, const char* text) {
	try {
		action();
	}
	catch (const std::invalid_argument& e) {
		if (std::strstr(e.what(), text))
			return true;
		std::fprintf(stderr, "message \"%s\" lacks \"%s\"\n", e.what(), text);
		return false;
	}
	std::fprintf(stderr, "nothing thrown; expected \"%s\"\n", text);
	return false;
}

/** Builds a plan as rackpy.dsp.graph lowers one. */
struct Builder {
	graph::PlanSpec spec;
	long long node(const char* op, std::vector<long long> inputs = {}, std::vector<double> args = {}) {
		spec.nodes.push_back({op, std::move(inputs), std::move(args), {}});
		return (long long) spec.nodes.size() - 1;
	}
	long long constant(double x) { return node("constant", {}, {x}); }
	long long control(const char* name, double value) {
		long long k = node("control", {}, {value});
		spec.controls.push_back({name, k, value});
		return k;
	}
	void feed(long long delay, long long source) { spec.latches.emplace_back(delay, source); }
};

std::unique_ptr<dsp::Hosted> hosted(std::shared_ptr<const KernelType> type) {
	auto h = std::make_unique<dsp::Hosted>();
	h->type = type.get();
	h->ownedType = type;
	h->values = graph::planDefaults(*type);
	h->kernel = type->create(h->values);
	return h;
}

rack::engine::Module::ProcessArgs frame(float sampleRate, int64_t n) {
	rack::engine::Module::ProcessArgs args;
	args.sampleRate = sampleRate;
	args.sampleTime = 1.f / sampleRate;
	args.frame = n;
	return args;
}

void connect(rack::engine::Port& port, int channels = 1) {
	port.channels = (uint8_t) channels;
}

/** A value as a graph node leaves it: rounded and stored, so that -funsafe-math-optimizations cannot
reassociate a twin's expression across what are separate nodes in the plan. */
float keep(float x) {
	rackpy::graph::settle(x);  // the compiled tier's own barrier, for each architecture
	return x;
}

/** Sample times the compiler cannot fold into constants (the plan reads them at run time). The references
multiply by a kept 1/rate, as a module multiplies by Rack's stored args.sampleTime: written as x * (1 / rate),
LLVM under -funsafe-math-optimizations may turn it into x / rate, which rounds differently (RP-40). */
volatile float RATE_44100 = 44100.f, RATE_48000 = 48000.f, RATE_1000 = 1000.f;

bool same(float a, float b) {
	return std::memcmp(&a, &b, sizeof a) == 0;
}

float noise(uint32_t& seed) {
	seed = seed * 1664525u + 1013904223u;
	return ((float) (seed >> 8) / 16777216.f - 0.5f) * 20.f;
}

/** Rack's tutorial oscillator: a sine at param 0 plus V/oct input 0, and a light blinking at 1 Hz. */
graph::PlanSpec tutorial() {
	Builder b;
	long long pitch = b.node("add", {b.node("param", {}, {0}), b.node("input", {}, {0})});
	long long phase = b.node("phasor", {b.node("pitch_to_freq", {pitch})});
	b.node("output", {b.node("mul", {b.constant(5), b.node("sin2pi", {phase})})}, {0});
	b.node("light", {b.node("lt", {b.node("phasor", {b.constant(1)}), b.constant(0.5)})}, {0});
	return b.spec;
}

void testTheTutorialOscillatorMatchesDirectCpp() {
	for (int channels : {0, 1, 4, 16}) {
		module::Module m;
		m.config(1, 1, 1, 1);
		m.kernels.attach(hosted(graph::planType(tutorial())), false);
		connect(m.inputs[0], channels);
		connect(m.outputs[0]);
		float phase[16] = {}, blink = 0.f;
		for (int64_t n = 0; n < 100000; n++) {
			for (int c = 0; c < channels; c++)
				m.inputs[0].setVoltage((float) ((n / 1000 + c) % 9) / 4.f - 1.f, c);
			m.params[0].setValue(0.25f);
			m.process(frame(44100.f, n));
			int lanes = std::max(1, channels);
			CHECK(m.outputs[0].getChannels() == lanes);
			for (int c = 0; c < lanes; c++) {
				// The tutorial's process(), written out (as RP-07's reference and rackpy.Sine compute it).
				float pitch = keep(m.params[0].getValue() + (channels ? m.inputs[0].getPolyVoltage(c) : 0.f));
				float freq = keep(rack::dsp::FREQ_C4 * std::pow(2.f, pitch));
				phase[c] += freq * keep(1.f / RATE_44100);  // a stored sample time, as args.sampleTime is
				if (phase[c] >= 1.f)
					phase[c] -= 1.f;
				if (!same(m.outputs[0].getVoltage(c), 5.f * keep(std::sin(2.f * float(M_PI) * phase[c])))) {
					CHECK(!"the tutorial oscillator differs from direct C++");
					return;
				}
			}
			blink += 1.f * keep(1.f / RATE_44100);
			if (blink >= 1.f)
				blink -= 1.f;
			if (!same(m.lights[0].getBrightness(), blink < 0.5f ? 1.f : 0.f)) {
				CHECK(!"the blinking light differs from direct C++");
				return;
			}
		}
	}
}

void testAVcaMatchesDirectCpp() {
	Builder b;
	b.node("output", {b.node("mul", {b.node("input", {}, {0}), b.node("param", {}, {0})})}, {0});
	module::Module m;
	m.config(1, 1, 1, 0);
	m.kernels.attach(hosted(graph::planType(b.spec)), false);
	connect(m.inputs[0]);
	connect(m.outputs[0]);
	uint32_t seed = 1;
	for (int64_t n = 0; n < 100000; n++) {
		int channels = 1 + (int) (n % 16);
		m.inputs[0].setChannels(channels);
		for (int c = 0; c < channels; c++)
			m.inputs[0].setVoltage(noise(seed), c);
		m.params[0].setValue((float) (n % 101) / 100.f);
		m.process(frame(48000.f, n));
		CHECK(m.outputs[0].getChannels() == channels);
		for (int c = 0; c < channels; c++)
			if (!same(m.outputs[0].getVoltage(c), m.inputs[0].getVoltage(c) * m.params[0].getValue())) {
				CHECK(!"the VCA differs from direct C++");
				return;
			}
	}
}

/** 16 voices: a sine per pitch channel, times a control changed mid-run through the slot's queue. */
void testSixteenVoicesAndAControlMatchDirectCpp() {
	Builder b;
	long long voices = b.node("sin2pi", {b.node("phasor", {b.node("pitch_to_freq", {b.node("input", {}, {0})})})});
	b.node("output", {b.node("mul", {voices, b.control("level", 5.0)})}, {0});
	auto type = graph::planType(b.spec);
	CHECK(type->controls == std::vector<std::string>{"level"});
	module::Module m;
	m.config(0, 1, 1, 0);
	auto h = hosted(type);
	dsp::Hosted* plan = h.get();
	m.kernels.attach(std::move(h), true);  // as when Rack's engine runs the module
	connect(m.inputs[0], 16);
	connect(m.outputs[0]);
	float phase[16] = {}, level = 5.f;
	for (int64_t n = 0; n < 20000; n++) {
		if (n == 7000) {
			CHECK(m.kernels.set(plan, 0, -1.25, true));  // applied at the next frame boundary
			level = -1.25f;
		}
		for (int c = 0; c < 16; c++)
			m.inputs[0].setVoltage((float) c / 8.f - 1.f + (float) (n % 300) / 600.f, c);
		m.process(frame(48000.f, n));
		CHECK(m.outputs[0].getChannels() == 16);
		for (int c = 0; c < 16; c++) {
			float freq = keep(rack::dsp::FREQ_C4 * std::pow(2.f, m.inputs[0].getVoltage(c)));
			phase[c] += freq * keep(1.f / RATE_48000);
			if (phase[c] >= 1.f)
				phase[c] -= 1.f;
			if (!same(m.outputs[0].getVoltage(c), keep(std::sin(2.f * float(M_PI) * phase[c])) * level)) {
				CHECK(!"16 voices differ from direct C++");
				return;
			}
		}
	}
}

void testFeedbackThroughADelayMatchesDirectCpp() {
	// y = y[-1] + (x - y[-1]) * 0.25, a one-pole lowpass; and a counter on a light.
	Builder b;
	long long y1 = b.node("delay", {}, {0.5});
	long long y = b.node("add", {y1, b.node("mul", {b.node("sub", {b.node("input", {}, {0}), y1}), b.constant(0.25)})});
	b.feed(y1, y);
	b.node("output", {y}, {0});
	long long count = b.node("delay", {}, {0});
	b.feed(count, b.node("add", {count, b.constant(1)}));
	b.node("light", {count}, {0});
	module::Module m;
	m.config(0, 1, 1, 1);
	m.kernels.attach(hosted(graph::planType(b.spec)), false);
	connect(m.inputs[0]);
	connect(m.outputs[0]);
	uint32_t seed = 7;
	float state = 0.5f;
	for (int64_t n = 0; n < 50000; n++) {
		m.inputs[0].setVoltage(noise(seed));
		m.process(frame(48000.f, n));
		state = state + keep(keep(m.inputs[0].getVoltage() - state) * 0.25f);
		if (!same(m.outputs[0].getVoltage(), state) || m.lights[0].getBrightness() != (float) n) {
			CHECK(!"feedback differs from direct C++");
			return;
		}
	}
}

void testTriggersAndPulsesMatchRack() {
	Builder b;
	long long edge = b.node("schmitt", {b.node("input", {}, {0})}, {0.1, 2.0});
	b.node("output", {edge}, {0});
	b.node("output", {b.node("pulse", {edge}, {0.01})}, {1});
	module::Module m;
	m.config(0, 1, 2, 0);
	m.kernels.attach(hosted(graph::planType(b.spec)), false);
	connect(m.inputs[0], 3);
	connect(m.outputs[0]);
	connect(m.outputs[1]);
	rack::dsp::SchmittTrigger trigger[3];
	rack::dsp::PulseGenerator pulse[3];
	uint32_t seed = 3;
	for (int64_t n = 0; n < 50000; n++) {
		for (int c = 0; c < 3; c++)
			m.inputs[0].setVoltage(noise(seed) / 4.f, c);
		m.process(frame(1000.f, n));
		for (int c = 0; c < 3; c++) {
			bool fired = trigger[c].process(m.inputs[0].getVoltage(c), 0.1f, 2.f);
			if (fired)
				pulse[c].trigger(0.01f);
			bool high = pulse[c].process(1.f / RATE_1000);
			if (m.outputs[0].getVoltage(c) != (fired ? 1.f : 0.f) || m.outputs[1].getVoltage(c) != (high ? 1.f : 0.f)) {
				CHECK(!"triggers or pulses differ from Rack's");
				return;
			}
		}
	}
}

void testFieldsWiringAndReset() {
	auto type = graph::planType(tutorial());
	CHECK(type->name == "rackpy.Graph");
	std::vector<std::string> names;
	for (const Field& f : type->fields)
		names.push_back(f.name);
	CHECK((names == std::vector<std::string>{"param[0]", "input[0]", "output[0]", "light[0]"}));
	CHECK(graph::planDefaults(*type) == (std::vector<double>{0, 0, 0, 0}));
	module::Module small;
	small.config(1, 1, 1, 0);  // no light
	CHECK(throwsInvalid([&] { dsp::checkWiring(*type, graph::planDefaults(*type), small); }, "light[0] = 0, but the module has 0 lights"));
	// Reset starts the nodes over; controls keep their values.
	Builder b;
	long long count = b.node("delay", {}, {2});
	b.feed(count, b.node("add", {count, b.control("step", 1)}));
	b.node("output", {count}, {0});
	module::Module m;
	m.config(0, 0, 1, 0);
	connect(m.outputs[0]);
	auto h = hosted(graph::planType(b.spec));
	dsp::Hosted* plan = h.get();
	m.kernels.attach(std::move(h), false);
	CHECK(m.kernels.set(plan, 0, 10, false));
	for (int64_t n = 0; n < 3; n++)
		m.process(frame(48000.f, n));
	CHECK(m.outputs[0].getVoltage() == 22.f);  // 2, 12, 22
	m.kernels.reset();
	m.process(frame(48000.f, 3));
	CHECK(m.outputs[0].getVoltage() == 2.f);
	m.process(frame(48000.f, 4));
	CHECK(m.outputs[0].getVoltage() == 12.f);
}

void testReplacementKeepsAndThenFreesThePlan() {
	module::Module m;
	m.config(0, 0, 1, 0);
	connect(m.outputs[0]);
	auto plan = [](double value) {
		Builder b;
		b.node("output", {b.constant(value)}, {0});
		return graph::planType(b.spec);
	};
	std::weak_ptr<const KernelType> first;
	{
		auto type = plan(1);
		first = type;
		m.kernels.attach(hosted(type), true);
	}
	m.process(frame(48000.f, 0));
	CHECK(m.outputs[0].getVoltage() == 1.f && !first.expired());  // the kernel owns its plan's type
	m.kernels.attach(hosted(plan(2)), true);
	CHECK(m.outputs[0].getVoltage() == 1.f);
	m.process(frame(48000.f, 1));
	CHECK(m.outputs[0].getVoltage() == 2.f && !first.expired());  // retired; the main thread has not reclaimed it
	m.kernels.reclaim();
	CHECK(first.expired());
}

void testBadPlansAreRefused() {
	auto refused = [](std::function<void(Builder&)> build, const char* text) {
		Builder b;
		build(b);
		return throwsInvalid([&] { graph::planType(b.spec); }, text);
	};
	CHECK(refused([](Builder&) {}, "1 to 4096 nodes"));
	CHECK(refused([](Builder& b) { b.node("output", {b.node("fft", {}, {})}, {0}); }, "(fft): not a node type"));
	CHECK(refused([](Builder& b) { b.node("output", {b.node("add", {b.constant(1)})}, {0}); }, "reads 2 signals, not 1"));
	CHECK(refused([](Builder& b) { b.node("output", {b.node("param", {}, {})}, {0}); }, "takes 1 numbers, not 0"));
	CHECK(refused([](Builder& b) { b.node("output", {5}, {0}); }, "reads node 5; a node reads only earlier nodes"));
	CHECK(refused([](Builder& b) { b.node("output", {0}, {0}); }, "reads node 0; a node reads only earlier nodes"));
	CHECK(refused([](Builder& b) {
		long long out = b.node("output", {b.constant(1)}, {0});
		b.node("light", {out}, {0});
	}, "which makes no signal"));
	CHECK(refused([](Builder& b) { b.node("output", {b.constant(NAN)}, {0}); }, "must be finite"));
	CHECK(refused([](Builder& b) { b.node("output", {b.constant(1)}, {-1}); }, "whole number from 0"));
	CHECK(refused([](Builder& b) { b.node("output", {b.constant(1)}, {0.5}); }, "whole number from 0"));
	CHECK(refused([](Builder& b) {
		b.node("output", {b.constant(1)}, {0});
		b.node("output", {b.constant(2)}, {0});
	}, "output 0 is already written"));
	CHECK(refused([](Builder& b) { b.constant(1); }, "writes no output, light or expander lane"));
	CHECK(refused([](Builder& b) { b.node("expander_send", {b.constant(1)}, {2, 0}); }, "its side is 0 (left) or 1 (right)"));
	CHECK(refused([](Builder& b) { b.node("expander_send", {b.constant(1)}, {1, 64}); }, "whole number from 0 to 63"));
	CHECK(refused([](Builder& b) { b.node("expander_send", {b.constant(1)}, {1, 0.5}); }, "whole number from 0 to 63"));
	CHECK(refused([](Builder& b) {
		b.node("expander_send", {b.constant(1)}, {1, 3});
		b.node("expander_send", {b.constant(2)}, {1, 3});
	}, "lane 3 of side 1 is already sent"));
	CHECK(refused([](Builder& b) {
		b.node("output", {b.node("expander_receive", {}, {0, 0})}, {0});
		b.node("light", {b.node("expander_receive", {}, {0, 0})}, {0});
	}, "lane 0 of side 0 is already received"));
	CHECK(refused([](Builder& b) { b.node("output", {b.node("expander_receive", {b.constant(1)}, {0, 0})}, {0}); },
		"reads 0 signals, not 1"));
	CHECK(refused([](Builder& b) { b.node("output", {b.node("pulse", {b.constant(1)}, {-1})}, {0}); }, "duration must not be negative"));
	CHECK(refused([](Builder& b) { b.node("output", {b.node("delay", {}, {0})}, {0}); }, "it is never fed"));
	CHECK(refused([](Builder& b) {
		long long d = b.node("delay", {}, {0});
		b.node("output", {d}, {0});
		b.feed(d, d);
		b.feed(d, d);
	}, "it is fed twice"));
	CHECK(refused([](Builder& b) {
		long long one = b.constant(1);
		b.node("output", {one}, {0});
		b.feed(one, one);
	}, "which is not a delay"));
	CHECK(refused([](Builder& b) {
		b.node("output", {b.node("control", {}, {1})}, {0});
	}, "(control): it has no name"));
	CHECK(refused([](Builder& b) {
		b.node("output", {b.control("two words", 1)}, {0});
	}, "must be an identifier"));
	CHECK(refused([](Builder& b) {
		long long k = b.control("gain", 1);
		b.spec.controls.push_back({"other", k, 1});
		b.node("output", {k}, {0});
	}, "must name a control node of its own"));
	CHECK(refused([](Builder& b) {
		b.node("output", {b.control("gain", 1)}, {0});
		b.node("light", {b.control("gain", 2)}, {0});
	}, "is named twice"));
	CHECK(refused([](Builder& b) {
		long long x = b.constant(0);
		for (size_t k = 0; k < graph::MAX_NODES; k++)
			x = b.node("neg", {x});
		b.node("output", {x}, {0});
	}, "1 to 4096 nodes"));
}

/** Native extension node types, registered as a kernel library's RACKPY_GRAPH_NODES would add them. */
struct Scale : graph::Node {
	float by;
	explicit Scale(float by) : by(by) {}
	void process(graph::Signal& out, const graph::Signal* const* in, const rack::engine::Module::ProcessArgs&) noexcept override {
		graph::map1(out, *in[0], [this](float x) { return x * by; });
	}
};

struct Count : graph::Node {
	float count = 0.f;
	void process(graph::Signal& out, const graph::Signal* const*, const rack::engine::Module::ProcessArgs&) noexcept override {
		graph::mono(out, count++);
	}
	void reset() noexcept override { count = 0.f; }
};

struct TooWide : graph::Node {
	void process(graph::Signal& out, const graph::Signal* const*, const rack::engine::Module::ProcessArgs&) noexcept override {
		out.channels = 99;
	}
};

void registerTestNodes() {
	graph::registerNodeType({"test.Scale", "x * by", 1, {"by", "offset"}, [](const std::vector<double>& a) -> std::unique_ptr<graph::Node> {
		if (a[1] != 0)
			throw std::invalid_argument("test.Scale: offset must be 0");
		return std::make_unique<Scale>((float) a[0]);
	}});
	graph::registerNodeType({"test.Count", "frames since reset", 0, {}, [](const std::vector<double>&) {
		return std::unique_ptr<graph::Node>(new Count);
	}});
	graph::registerNodeType({"test.TooWide", "claims 99 channels", 0, {}, [](const std::vector<double>&) {
		return std::unique_ptr<graph::Node>(new TooWide);
	}});
}

void testExtensionNodes() {
	CHECK(graph::findNodeType("test.Scale") && graph::nodeTypes().size() == 3);
	Builder b;
	long long in = b.node("input", {}, {0});
	b.spec.nodes.push_back({"test.Scale", {in}, {0.0, 2.5}, {"offset", "by"}});  // by name, in any order
	long long scaled = (long long) b.spec.nodes.size() - 1;
	b.spec.nodes.push_back({"test.Count", {}, {}, {}});
	long long count = (long long) b.spec.nodes.size() - 1;
	b.node("output", {b.node("add", {scaled, count})}, {0});
	module::Module m;
	m.config(0, 1, 1, 0);
	m.kernels.attach(hosted(graph::planType(b.spec)), false);
	connect(m.inputs[0], 4);
	connect(m.outputs[0]);
	uint32_t seed = 11;
	for (int64_t n = 0; n < 1000; n++) {
		for (int c = 0; c < 4; c++)
			m.inputs[0].setVoltage(noise(seed), c);
		m.process(frame(48000.f, n));
		for (int c = 0; c < 4; c++)
			if (!same(m.outputs[0].getVoltage(c), keep(m.inputs[0].getVoltage(c) * 2.5f) + (float) n)) {
				CHECK(!"an extension node differs from direct C++");
				return;
			}
	}
	m.kernels.reset();  // reaches the extension's reset
	m.process(frame(48000.f, 1000));
	CHECK(m.outputs[0].getVoltage(0) == keep(m.inputs[0].getVoltage(0) * 2.5f));
	// A node that claims more channels than a signal holds fails the plan instead of being read past.
	Builder wide;
	wide.spec.nodes.push_back({"test.TooWide", {}, {}, {}});
	wide.node("output", {wide.node("neg", {0})}, {0});
	module::Module w;
	w.config(0, 0, 1, 0);
	connect(w.outputs[0]);
	w.kernels.attach(hosted(graph::planType(wide.spec)), false);
	w.process(frame(48000.f, 0));
	CHECK(w.kernels.failed() && w.outputs[0].getVoltage() == 0.f);
	// Refusals.
	auto refused = [](graph::PlanSpec::Node node, const char* text) {
		Builder r;
		long long x = r.constant(1);
		for (long long& i : node.inputs)
			i = x;
		r.spec.nodes.push_back(std::move(node));
		r.node("output", {(long long) r.spec.nodes.size() - 1}, {0});
		return throwsInvalid([&] { graph::planType(r.spec); }, text);
	};
	CHECK(refused({"test.Missing", {}, {}, {}}, "no native node type has this name"));
	CHECK(refused({"test.Scale", {0, 0}, {1, 0}, {"by", "offset"}}, "reads 1 signals, not 2"));
	CHECK(refused({"test.Scale", {0}, {1}, {"by"}}, "needs \"offset\""));
	CHECK(refused({"test.Scale", {0}, {1, 0}, {"by", "by"}}, "is given \"by\" twice"));
	CHECK(refused({"test.Scale", {0}, {1, 0, 2}, {"by", "offset", "gain"}}, "takes no number \"gain\""));
	CHECK(refused({"test.Scale", {0}, {1, 0}, {"by"}}, "needs a name for each of its numbers"));
	CHECK(refused({"test.Scale", {0}, {NAN, 0}, {"by", "offset"}}, "must be finite"));
	Builder throwing;
	long long x = throwing.constant(1);
	throwing.spec.nodes.push_back({"test.Scale", {x}, {1.0, 3.0}, {"by", "offset"}});
	throwing.node("output", {1}, {0});
	auto type = graph::planType(throwing.spec);  // the plan is well formed; the type refuses when the kernel is made
	CHECK(throwsInvalid([&] { type->create(graph::planDefaults(*type)); }, "offset must be 0"));
}

}  // namespace

// --- Expander exchange (RP-23B) -------------------------------------------------------------

/** Gives `side` of `m` two "rackpy.graph" buffers of `lanes` lanes, as Python's graph_messages() does. */
void giveMessages(module::Module& m, uint8_t side, size_t lanes, const char* schema = engine::GRAPH_SCHEMA) {
	auto a = engine::allocateMessage(schema, lanes * graph::LANE), b = engine::allocateMessage(schema, lanes * graph::LANE);
	rack::engine::Module::Expander& e = m.getExpander(side);
	e.producerMessage = a->data;
	e.consumerMessage = b->data;
	m.expanderMessages.push_back(a);
	m.expanderMessages.push_back(b);
	engine::publishChannel(m, m, side);
}

/** Puts `left` and `right` side by side, as Rack's engine does from the expanders' module IDs. */
void place(rack::engine::Module& left, rack::engine::Module& right) {
	left.rightExpander.moduleId = right.id;
	left.rightExpander.module = &right;
	right.leftExpander.moduleId = left.id;
	right.leftExpander.module = &left;
}

/** One frame of Rack's engine (Engine_stepFrame): every module in `order`, then the requested flips. */
void stepFrame(const std::vector<rack::engine::Module*>& order, int64_t n) {
	for (rack::engine::Module* m : order)
		m->process(frame(48000.f, n));
	for (rack::engine::Module* m : order)
		for (rack::engine::Module::Expander* e : {&m->leftExpander, &m->rightExpander})
			if (e->messageFlipRequested) {
				std::swap(e->producerMessage, e->consumerMessage);
				e->messageFlipRequested = false;
			}
}

/** A counter from 1 (frame n: n + 1) sent right on lane `lane`, and a 16-channel ramp on lane 1. */
graph::PlanSpec sender(double lane = 0) {
	Builder b;
	long long count = b.node("delay", {}, {1});
	b.feed(count, b.node("add", {count, b.constant(1)}));
	b.node("expander_send", {count}, {1, lane});
	b.node("expander_send", {b.node("input", {}, {0})}, {1, 1});
	return b.spec;
}

/** Receives lanes 0 and 1 from the left onto outputs 0 and 1. */
graph::PlanSpec receiver() {
	Builder b;
	b.node("output", {b.node("expander_receive", {}, {0, 0})}, {0});
	b.node("output", {b.node("expander_receive", {}, {0, 1})}, {1});
	return b.spec;
}

struct Pair {
	module::Module a, b;
	Pair() {
		a.id = 11;
		b.id = 12;
		a.config(0, 1, 0, 0);
		b.config(0, 0, 2, 0);
		a.kernels.attach(hosted(graph::planType(sender())), false);
		b.kernels.attach(hosted(graph::planType(receiver())), false);
		connect(a.inputs[0], 16);
		connect(b.outputs[0]);
		connect(b.outputs[1]);
		giveMessages(b, 0, 2);
		place(a, b);
	}
	void drive(int64_t n) {
		for (int c = 0; c < 16; c++)
			a.inputs[0].setVoltage((float) (n * 16 + c), c);
	}
};

void testOneFrameOfLatencyInEitherOrder() {
	for (bool senderFirst : {true, false}) {
		Pair p;
		std::vector<rack::engine::Module*> order = senderFirst ? std::vector<rack::engine::Module*>{&p.a, &p.b}
			: std::vector<rack::engine::Module*>{&p.b, &p.a};
		for (int64_t n = 0; n < 1000; n++) {
			p.drive(n);
			stepFrame(order, n);
			if (n == 0) {
				CHECK(p.b.outputs[0].getVoltage() == 0.f && p.b.outputs[1].getChannels() == 1);  // nothing yet: silence
				continue;
			}
			// What A sent during frame n - 1: its counter (n) and the ramp it read.
			CHECK(p.b.outputs[0].getChannels() == 1 && p.b.outputs[0].getVoltage() == (float) n);
			CHECK(p.b.outputs[1].getChannels() == 16);
			for (int c = 0; c < 16; c++)
				if (p.b.outputs[1].getVoltage(c) != (float) ((n - 1) * 16 + c)) {
					CHECK(!"a polyphonic lane did not arrive one frame later");
					return;
				}
		}
	}
}

void testMismatchedOrMissingNeighboursExchangeNothing() {
	// The receiver's buffers have another schema: nothing is sent (no flip), silence is received.
	{
		Pair p;
		p.b.expanderMessages.clear();
		giveMessages(p.b, 0, 2, "demo.Other");
		void* consumer = p.b.leftExpander.consumerMessage;
		for (int64_t n = 0; n < 3; n++) {
			p.drive(n);
			stepFrame({&p.a, &p.b}, n);
			CHECK(p.b.leftExpander.consumerMessage == consumer);
			CHECK(p.b.outputs[0].getVoltage() == 0.f && p.b.outputs[1].getChannels() == 1);
		}
	}
	// One lane only: lane 0 still arrives, lane 1 does not fit and reads silence.
	{
		Pair p;
		p.b.expanderMessages.clear();
		giveMessages(p.b, 0, 1);
		for (int64_t n = 0; n < 3; n++) {
			p.drive(n);
			stepFrame({&p.a, &p.b}, n);
			CHECK(p.b.outputs[0].getVoltage() == (float) n);
			CHECK(p.b.outputs[1].getVoltage() == 0.f && p.b.outputs[1].getChannels() == 1);
		}
	}
	// Another plugin's module (not a RackPy module) between them: neither side reaches it.
	{
		Pair p;
		rack::engine::Module foreign;
		foreign.id = 13;
		place(p.a, foreign);
		place(foreign, p.b);
		for (int64_t n = 0; n < 3; n++) {
			p.drive(n);
			stepFrame({&p.a, &foreign, &p.b}, n);
			CHECK(!foreign.leftExpander.producerMessage && !foreign.leftExpander.consumerMessage);
			CHECK(p.b.outputs[0].getVoltage() == 0.f);
		}
	}
	// The receiver's left neighbour is another module: the sender does not write into it.
	{
		Pair p;
		module::Module other;
		other.id = 14;
		p.b.leftExpander.module = &other;
		p.b.leftExpander.moduleId = other.id;
		void* consumer = p.b.leftExpander.consumerMessage;
		for (int64_t n = 0; n < 3; n++) {
			p.drive(n);
			stepFrame({&p.a, &p.b, &other}, n);
			CHECK(p.b.leftExpander.consumerMessage == consumer && p.b.outputs[0].getVoltage() == 0.f);
		}
	}
}

void testRemovalAndAStoppedSenderGiveSilence() {
	// Rack's removeModule clears the neighbour's pointer: a message already flipped is not read.
	{
		Pair p;
		for (int64_t n = 0; n < 5; n++) {
			p.drive(n);
			stepFrame({&p.a, &p.b}, n);
		}
		CHECK(p.b.outputs[0].getVoltage() == 4.f);
		p.b.leftExpander.module = nullptr;
		p.b.leftExpander.moduleId = -1;
		stepFrame({&p.b}, 5);
		CHECK(p.b.outputs[0].getVoltage() == 0.f && p.b.outputs[1].getVoltage(0) == 0.f);
	}
	// A sender that stops (its right neighbour is gone) leaves no flip: the last message is read once.
	{
		Pair p;
		for (int64_t n = 0; n < 5; n++) {
			p.drive(n);
			stepFrame({&p.a, &p.b}, n);
		}
		p.a.rightExpander.module = nullptr;
		p.a.rightExpander.moduleId = -1;
		stepFrame({&p.a, &p.b}, 5);
		CHECK(p.b.outputs[0].getVoltage() == 5.f);  // sent during frame 4
		stepFrame({&p.a, &p.b}, 6);
		CHECK(p.b.outputs[0].getVoltage() == 0.f && p.b.outputs[1].getChannels() == 1);  // read once, then silence
	}
	// Another receiver in its place is found at the next frame.
	{
		Pair p;
		for (int64_t n = 0; n < 3; n++)
			stepFrame({&p.a, &p.b}, n);
		module::Module next;
		next.id = 15;
		next.config(0, 0, 2, 0);
		next.kernels.attach(hosted(graph::planType(receiver())), false);
		connect(next.outputs[0]);
		giveMessages(next, 0, 2);
		place(p.a, next);
		p.b.leftExpander.module = nullptr;
		stepFrame({&p.a, &next}, 3);
		stepFrame({&p.a, &next}, 4);
		CHECK(next.outputs[0].getVoltage() == 4.f);
	}
}

void testAPlanThatOnlySendsIsAccepted() {
	Builder b;
	b.node("expander_send", {b.constant(1)}, {0, 63});
	auto type = graph::planType(b.spec);
	CHECK(type->fields.empty());
}

int main() {
	runtime::markMainThread();
	testTheTutorialOscillatorMatchesDirectCpp();
	testAVcaMatchesDirectCpp();
	testSixteenVoicesAndAControlMatchDirectCpp();
	testFeedbackThroughADelayMatchesDirectCpp();
	testTriggersAndPulsesMatchRack();
	testFieldsWiringAndReset();
	testReplacementKeepsAndThenFreesThePlan();
	testBadPlansAreRefused();
	registerTestNodes();
	testExtensionNodes();
	testOneFrameOfLatencyInEitherOrder();
	testMismatchedOrMissingNeighboursExchangeNothing();
	testRemovalAndAStoppedSenderGiveSilence();
	testAPlanThatOnlySendsIsAccepted();
	if (failures)
		std::fprintf(stderr, "%d check(s) failed\n", failures);
	else
		std::printf("graph plans: all checks passed\n");
	return failures ? 1 : 0;
}
