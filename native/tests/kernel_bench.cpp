// SPDX-License-Identifier: GPL-3.0-or-later
// RP-17: RackPy's production kernel path against the same DSP written as a
// plain C++ module, per process() call, as RP-07 measured the spike's shape
// (benchmarks/spike/src/bench.cpp). One frame per call, nothing batched,
// randomized A/B order, the SDK's flags. Prints JSON; tests do not assert
// timings (the planning machine is not controlled hardware).
#include "../module/module.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace rackpy;

namespace {

/** RP-07's reference sine module, written directly. */
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

struct NativeGain : rack::engine::Module {
	NativeGain() { config(1, 1, 1, 0); }
	void process(const ProcessArgs&) override { outputs[0].setVoltage(inputs[0].getVoltage() * params[0].getValue()); }
};

/** The same DSP as NativeSine and NativeGain inside kernels: what RackPy itself costs. */
struct SameSine : Kernel {
	float phase = 0.f;
	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {
		float pitch = m.inputs[0].getVoltage();
		float freq = rack::dsp::FREQ_C4 * std::pow(2.f, pitch);
		phase += freq * args.sampleTime;
		if (phase >= 1.f)
			phase -= 1.f;
		m.outputs[0].setVoltage(5.f * std::sin(2.f * float(M_PI) * phase));
		return true;
	}
};

struct SameGain : Kernel {
	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs&) noexcept override {
		m.outputs[0].setVoltage(m.inputs[0].getVoltage() * m.params[0].getValue());
		return true;
	}
};

template <typename M>
double nsPerFrame(M& m, int frames) {
	rack::engine::Module::ProcessArgs args;
	args.sampleRate = 48000.f;
	args.sampleTime = 1.f / 48000.f;
	auto start = std::chrono::steady_clock::now();
	for (int n = 0; n < frames; n++) {
		args.frame = n;
		m.inputs[0].setVoltage((float) (n & 255) / 256.f);
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
	return v[(size_t) (v.size() * 0.99) < v.size() ? (size_t) (v.size() * 0.99) : v.size() - 1];
}

}  // namespace

int main() {
	runtime::markMainThread();
	dsp::registerBuiltins();
	const int frames = 512000, reps = 31;
	struct Pair { const char* name; rack::engine::Module* native; module::Module* rackpy; };
	NativeSine sine;
	NativeGain gain;
	module::Module kSine, kGain, sSine, sGain;
	kSine.config(0, 1, 1, 0);
	kGain.config(1, 1, 1, 0);
	sSine.config(0, 1, 1, 0);
	sGain.config(1, 1, 1, 0);
	for (auto* m : {(rack::engine::Module*) &sine, (rack::engine::Module*) &gain, (rack::engine::Module*) &kSine,
		(rack::engine::Module*) &kGain, (rack::engine::Module*) &sSine, (rack::engine::Module*) &sGain}) {
		m->inputs[0].channels = 1;
		m->outputs[0].channels = 1;
	}
	gain.params[0].setValue(0.5f);
	kGain.params[0].setValue(0.5f);
	sGain.params[0].setValue(0.5f);
	auto hosted = [](const char* name, std::vector<double> values) {
		auto h = std::make_unique<dsp::Hosted>();
		h->type = dsp::findType(name);
		h->kernel = h->type->create(values);
		return h;
	};
	kSine.kernels.attach(hosted("rackpy.Sine", {0, 0, -1, 5}), true);  // through the engine handover
	kGain.kernels.attach(hosted("rackpy.Gain", {0, 0, 0, 1}), true);
	auto same = [](Kernel* k) {
		auto h = std::make_unique<dsp::Hosted>();
		h->kernel.reset(k);
		return h;
	};
	sSine.kernels.attach(same(new SameSine), true);
	sGain.kernels.attach(same(new SameGain), true);
	// "same_*": RackPy's fixed cost (identical DSP). "builtin_*": RackPy's polyphonic
	// built-in kernels against the mono reference, so their extra work is included.
	std::vector<Pair> pairs = {{"same_sine", &sine, &sSine}, {"same_gain", &gain, &sGain},
		{"builtin_sine", &sine, &kSine}, {"builtin_gain", &gain, &kGain}};
	std::mt19937 rng(1234);
	std::printf("{\"frames_per_rep\": %d, \"reps\": %d, \"results\": [", frames, reps);
	for (size_t p = 0; p < pairs.size(); p++) {
		std::vector<double> native, rackpy, diff;
		nsPerFrame(*pairs[p].native, frames);  // warm up both
		nsPerFrame(*pairs[p].rackpy, frames);
		for (int r = 0; r < reps; r++) {
			bool nativeFirst = rng() & 1;
			double a = 0, b = 0;
			if (nativeFirst) {
				a = nsPerFrame(*pairs[p].native, frames);
				b = nsPerFrame(*pairs[p].rackpy, frames);
			}
			else {
				b = nsPerFrame(*pairs[p].rackpy, frames);
				a = nsPerFrame(*pairs[p].native, frames);
			}
			native.push_back(a);
			rackpy.push_back(b);
			diff.push_back(b - a);
		}
		std::printf("%s{\"workload\": \"%s\", \"native_p50_ns\": %.3f, \"rackpy_p50_ns\": %.3f, "
			"\"overhead_p50_ns\": %.3f, \"overhead_p99_ns\": %.3f}",
			p ? ", " : "", pairs[p].name, median(native), median(rackpy), median(diff), p99(diff));
	}
	std::printf("]}\n");
	return 0;
}
