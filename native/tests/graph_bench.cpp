// SPDX-License-Identifier: GPL-3.0-or-later
// RP-21B: the plan interpreter against the same DSP hand-fused into a plain
// C++ module, per process() call, as RP-07 and RP-17 measured (ADR 004's
// proposed budget for plans: at most 1.5 ns per frame per plan). One frame
// per call, nothing batched, randomized A/B order, the SDK's flags. Prints
// JSON; tests do not assert timings (the planning machine is not controlled
// hardware).
#include "../dsp/graph/plan.hpp"
#include "../module/module.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace rackpy;

namespace {

/** RP-07's reference sine module, written directly (as kernel_bench.cpp). */
struct NativeSine : rack::engine::Module {
	float phase = 0.f;
	NativeSine() { config(0, 1, 1, 0); }
	void process(const ProcessArgs& args) override {
		float pitch = inputs[0].getVoltage();
		float freq = rack::dsp::FREQ_C4 * std::pow(2.f, pitch);
		phase += freq * args.sampleTime;
		if (phase >= 1.f)
			phase -= 1.f;
		outputs[0].setVoltage(5.f * std::sin(2.f * float(M_PI) * phase));
	}
};

/** RP-07's sine -> gain chain, fused by hand. */
struct NativeSineGain : rack::engine::Module {
	float phase = 0.f;
	NativeSineGain() { config(1, 1, 1, 0); }
	void process(const ProcessArgs& args) override {
		float pitch = inputs[0].getVoltage();
		float freq = rack::dsp::FREQ_C4 * std::pow(2.f, pitch);
		phase += freq * args.sampleTime;
		if (phase >= 1.f)
			phase -= 1.f;
		outputs[0].setVoltage(5.f * std::sin(2.f * float(M_PI) * phase) * params[0].getValue());
	}
};

struct NativeGain : rack::engine::Module {
	NativeGain() { config(1, 1, 1, 0); }
	void process(const ProcessArgs&) override { outputs[0].setVoltage(inputs[0].getVoltage() * params[0].getValue()); }
};

/** 16 voices of the reference sine. */
struct NativePoly : rack::engine::Module {
	float phase[16] = {};
	NativePoly() { config(0, 1, 1, 0); }
	void process(const ProcessArgs& args) override {
		// The input's count, as the plan's output takes it (a literal 16 trips GCC's -Warray-bounds in Port::setChannels).
		int n = inputs[0].getChannels();
		outputs[0].setChannels(n);
		for (int c = 0; c < n; c++) {
			float freq = rack::dsp::FREQ_C4 * std::pow(2.f, inputs[0].getVoltage(c));
			phase[c] += freq * args.sampleTime;
			if (phase[c] >= 1.f)
				phase[c] -= 1.f;
			outputs[0].setVoltage(5.f * std::sin(2.f * float(M_PI) * phase[c]), c);
		}
	}
};

struct Builder {
	graph::PlanSpec spec;
	long long node(const char* op, std::vector<long long> inputs = {}, std::vector<double> args = {}) {
		spec.nodes.push_back({op, std::move(inputs), std::move(args)});
		return (long long) spec.nodes.size() - 1;
	}
};

/** 5 sin(2 pi phasor(pitch_to_freq(input 0))), optionally times param 0. */
graph::PlanSpec sinePlan(bool gain) {
	Builder b;
	long long sine = b.node("mul", {b.node("constant", {}, {5}),
		b.node("sin2pi", {b.node("phasor", {b.node("pitch_to_freq", {b.node("input", {}, {0})})})})});
	if (gain)
		sine = b.node("mul", {sine, b.node("param", {}, {0})});
	b.node("output", {sine}, {0});
	return b.spec;
}

graph::PlanSpec vcaPlan() {
	Builder b;
	b.node("output", {b.node("mul", {b.node("input", {}, {0}), b.node("param", {}, {0})})}, {0});
	return b.spec;
}

std::unique_ptr<dsp::Hosted> hosted(const graph::PlanSpec& spec) {
	auto type = graph::planType(spec);
	auto h = std::make_unique<dsp::Hosted>();
	h->type = type.get();
	h->ownedType = type;
	h->values = graph::planDefaults(*type);
	h->kernel = type->create(h->values);
	return h;
}

template <typename M>
double nsPerFrame(M& m, int frames) {
	rack::engine::Module::ProcessArgs args;
	args.sampleRate = 48000.f;
	args.sampleTime = 1.f / 48000.f;
	int channels = std::max(1, m.inputs[0].getChannels());
	auto start = std::chrono::steady_clock::now();
	for (int n = 0; n < frames; n++) {
		args.frame = n;
		for (int c = 0; c < channels; c++)
			m.inputs[0].setVoltage((float) ((n + c) & 255) / 256.f, c);
		m.process(args);
	}
	auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
	return ns / frames;
}

double median(std::vector<double> v) {
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}

double p99(std::vector<double> v) {
	std::sort(v.begin(), v.end());
	return v[std::min(v.size() - 1, (size_t) (v.size() * 0.99))];
}

}  // namespace

int main() {
	runtime::markMainThread();
	const int frames = 512000, reps = 31;
	NativeSine sine;
	NativeSineGain sineGain;
	NativeGain gain;
	NativePoly poly;
	module::Module pSine, pSineGain, pGain, pPoly;
	pSine.config(0, 1, 1, 0);
	pSineGain.config(1, 1, 1, 0);
	pGain.config(1, 1, 1, 0);
	pPoly.config(0, 1, 1, 0);
	for (rack::engine::Module* m : std::initializer_list<rack::engine::Module*>{&sine, &sineGain, &gain, &poly, &pSine,
			&pSineGain, &pGain, &pPoly}) {
		m->inputs[0].channels = 1;
		m->outputs[0].channels = 1;
		if (!m->params.empty())
			m->params[0].setValue(0.5f);
	}
	poly.inputs[0].channels = pPoly.inputs[0].channels = 16;
	pSine.kernels.attach(hosted(sinePlan(false)), true);  // through the engine handover
	pSineGain.kernels.attach(hosted(sinePlan(true)), true);
	pGain.kernels.attach(hosted(vcaPlan()), true);
	pPoly.kernels.attach(hosted(sinePlan(false)), true);
	struct Pair { const char* name; rack::engine::Module* native; module::Module* plan; int nodes; };
	std::vector<Pair> pairs = {{"plan_sine", &sine, &pSine, 7}, {"plan_sine_gain", &sineGain, &pSineGain, 9},
		{"plan_vca", &gain, &pGain, 4}, {"plan_poly16", &poly, &pPoly, 7}};
	std::mt19937 rng(1234);
	std::printf("{\"frames_per_rep\": %d, \"reps\": %d, \"results\": [", frames, reps);
	for (size_t p = 0; p < pairs.size(); p++) {
		std::vector<double> native, plan, diff;
		nsPerFrame(*pairs[p].native, frames);  // warm up both
		nsPerFrame(*pairs[p].plan, frames);
		for (int r = 0; r < reps; r++) {
			double a = 0, b = 0;
			if (rng() & 1) {
				a = nsPerFrame(*pairs[p].native, frames);
				b = nsPerFrame(*pairs[p].plan, frames);
			}
			else {
				b = nsPerFrame(*pairs[p].plan, frames);
				a = nsPerFrame(*pairs[p].native, frames);
			}
			native.push_back(a);
			plan.push_back(b);
			diff.push_back(b - a);
		}
		std::printf("%s{\"workload\": \"%s\", \"nodes\": %d, \"native_p50_ns\": %.3f, \"plan_p50_ns\": %.3f, "
			"\"overhead_p50_ns\": %.3f, \"overhead_p99_ns\": %.3f}",
			p ? ", " : "", pairs[p].name, pairs[p].nodes, median(native), median(plan), median(diff), p99(diff));
	}
	std::printf("]}\n");
	return 0;
}
