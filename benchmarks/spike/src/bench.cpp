// SPDX-License-Identifier: GPL-3.0-or-later
// RP-07 standalone benchmark: plain C++ modules against the proposed RackPy
// production shape (a generic module that drains a command queue and calls a
// native kernel chosen and configured from Python). Modules are driven one
// process() call per frame, exactly as Rack does; nothing is batched.
// Spike code, not RackPy.
//
//   bench time         JSON timings (randomized A/B order)
//   bench equivalence  JSON bit-exact output comparison
//   bench guard        JSON audio-path event counts (needs LD_PRELOAD=audio_guard.so)
#include "dsp.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <tuple>
#include <vector>

using namespace rack;
using namespace bench;

namespace {

constexpr float SAMPLE_RATE = 48000.f;
constexpr int BLOCK = 256;
constexpr int INPUT_TABLE = 4096;
float pitchTable[INPUT_TABLE];

// --- native modules ---------------------------------------------------------------

struct NativeTrivial : engine::Module {
	NativeTrivial() { config(1, 1, 1, 0); }
	void process(const ProcessArgs& args) override { outputs[0].setVoltage(inputs[0].getVoltage()); }
};

struct NativeGain : engine::Module {
	NativeGain() {
		config(1, 1, 1, 0);
		configParam(0, 0.f, 2.f, 0.8f);
	}
	void process(const ProcessArgs& args) override {
		outputs[0].setVoltage(gain(inputs[0].getVoltage(), params[0].getValue()));
	}
};

struct NativeSine : engine::Module {
	Sine osc;
	NativeSine() {
		config(1, 1, 1, 0);
		configParam(0, -2.f, 2.f, 0.f);
	}
	void process(const ProcessArgs& args) override {
		outputs[0].setVoltage(osc.step(params[0].getValue() + inputs[0].getVoltage(), args.sampleTime));
	}
};

struct NativePoly : engine::Module {
	PolySine osc;
	NativePoly() { config(1, 1, 1, 0); }
	void process(const ProcessArgs& args) override {
		int channels = std::max(1, (int) inputs[0].getChannels());
		float out[16];
		osc.step(inputs[0].getVoltages(), out, channels, args.sampleTime);
		outputs[0].setChannels(channels);
		outputs[0].writeVoltages(out);
	}
};

struct NativePolySimd : engine::Module {
	PolySineSimd osc;
	NativePolySimd() { config(1, 1, 1, 0); }
	void process(const ProcessArgs& args) override {
		int channels = std::max(1, (int) inputs[0].getChannels());
		alignas(16) float out[16];
		osc.step(inputs[0].getVoltages(), out, channels, args.sampleTime);
		outputs[0].setChannels(channels);
		outputs[0].writeVoltages(out);
	}
};

/** Fused sine -> gain chain, the native reference for the plan executor. */
struct NativeChain : engine::Module {
	Sine osc;
	NativeChain() {
		config(2, 1, 1, 0);
		configParam(0, -2.f, 2.f, 0.f);
		configParam(1, 0.f, 2.f, 0.8f);
	}
	void process(const ProcessArgs& args) override {
		float s = osc.step(params[0].getValue() + inputs[0].getVoltage(), args.sampleTime);
		outputs[0].setVoltage(gain(s, params[1].getValue()));
	}
};

// --- RackPy production shape -------------------------------------------------------

/** What a native kernel sees. Python chose and configured the kernel on the UI thread. */
struct KernelContext {
	engine::Module* module;
	const engine::Module::ProcessArgs* args;
	const float* mirror;  // values mirrored from UI commands
};

struct Kernel {
	virtual ~Kernel() = default;
	virtual void process(const KernelContext& k) = 0;
};

struct RackPyModule : engine::Module {
	std::unique_ptr<Kernel> kernel;
	SpscRing<Command, 256> commands;
	float mirror[16] = {};

	/** `defaults` match the native module's parameters exactly. */
	RackPyModule(std::vector<float> defaults, std::unique_ptr<Kernel> k) : kernel(std::move(k)) {
		config((int) std::max<size_t>(1, defaults.size()), 1, 1, 0);
		for (size_t i = 0; i < defaults.size(); i++)
			configParam((int) i, -2.f, 2.f, defaults[i]);
	}
	void process(const ProcessArgs& args) override {
		Command c;
		while (commands.pop(c))  // empty on the steady path: one acquire load
			mirror[c.slot & 15] = c.value;
		kernel->process(KernelContext{this, &args, mirror});
	}
};

struct TrivialKernel : Kernel {
	void process(const KernelContext& k) override {
		k.module->outputs[0].setVoltage(k.module->inputs[0].getVoltage());
	}
};
struct GainKernel : Kernel {
	void process(const KernelContext& k) override {
		k.module->outputs[0].setVoltage(gain(k.module->inputs[0].getVoltage(), k.module->params[0].getValue()));
	}
};
struct SineKernel : Kernel {
	Sine osc;
	void process(const KernelContext& k) override {
		auto* m = k.module;
		m->outputs[0].setVoltage(osc.step(m->params[0].getValue() + m->inputs[0].getVoltage(), k.args->sampleTime));
	}
};
struct PolyKernel : Kernel {
	PolySine osc;
	void process(const KernelContext& k) override {
		auto* m = k.module;
		int channels = std::max(1, (int) m->inputs[0].getChannels());
		float out[16];
		osc.step(m->inputs[0].getVoltages(), out, channels, k.args->sampleTime);
		m->outputs[0].setChannels(channels);
		m->outputs[0].writeVoltages(out);
	}
};
struct PolySimdKernel : Kernel {
	PolySineSimd osc;
	void process(const KernelContext& k) override {
		auto* m = k.module;
		int channels = std::max(1, (int) m->inputs[0].getChannels());
		alignas(16) float out[16];
		osc.step(m->inputs[0].getVoltages(), out, channels, k.args->sampleTime);
		m->outputs[0].setChannels(channels);
		m->outputs[0].writeVoltages(out);
	}
};

/** RP-21 shape: a precompiled list of node kernels run once per frame, in order. */
struct Node {
	virtual ~Node() = default;
	virtual void run(const KernelContext& k, float* scratch) = 0;
};
struct SineNode : Node {
	Sine osc;
	void run(const KernelContext& k, float* s) override {
		s[0] = osc.step(k.module->params[0].getValue() + k.module->inputs[0].getVoltage(), k.args->sampleTime);
	}
};
struct GainNode : Node {
	void run(const KernelContext& k, float* s) override { s[1] = gain(s[0], k.module->params[1].getValue()); }
};
struct OutputNode : Node {
	void run(const KernelContext& k, float* s) override { k.module->outputs[0].setVoltage(s[1]); }
};
struct PlanKernel : Kernel {
	std::vector<std::unique_ptr<Node>> nodes;
	float scratch[4] = {};
	PlanKernel() {
		nodes.emplace_back(new SineNode);
		nodes.emplace_back(new GainNode);
		nodes.emplace_back(new OutputNode);
	}
	void process(const KernelContext& k) override {
		for (auto& n : nodes)
			n->run(k, scratch);
	}
};

/** The same plan composed at compile time: static dispatch, one virtual call per frame.
RackPy would instantiate such compositions from a registry of native node types. */
struct StaticSine {
	Sine osc;
	void run(const KernelContext& k, float* s) {
		s[0] = osc.step(k.module->params[0].getValue() + k.module->inputs[0].getVoltage(), k.args->sampleTime);
	}
};
struct StaticGain {
	void run(const KernelContext& k, float* s) { s[1] = gain(s[0], k.module->params[1].getValue()); }
};
struct StaticOutput {
	void run(const KernelContext& k, float* s) { k.module->outputs[0].setVoltage(s[1]); }
};
template <typename... Nodes>
struct StaticPlanKernel : Kernel {
	std::tuple<Nodes...> nodes;
	float scratch[4] = {};
	void process(const KernelContext& k) override {
		std::apply([&](auto&... n) { (n.run(k, scratch), ...); }, nodes);
	}
};

// Deliberate violations, used only to prove the guard catches them. They go
// through volatile function pointers and a persisted object, because GCC at -O3
// legally elides a new/delete pair (a std::vector temporary was optimized away).
void* (*volatile allocFn)(size_t) = std::malloc;
void (*volatile freeFn)(void*) = std::free;
struct InjectMallocKernel : Kernel {
	void process(const KernelContext& k) override {
		float* tmp = static_cast<float*>(allocFn(32 * sizeof(float)));
		tmp[3] = k.module->inputs[0].getVoltage();
		k.module->outputs[0].setVoltage(tmp[3]);
		freeFn(tmp);
	}
};
std::vector<float>* volatile newSink = nullptr;
struct InjectNewKernel : Kernel {
	void process(const KernelContext& k) override {
		delete newSink;
		newSink = new std::vector<float>(8, k.module->inputs[0].getVoltage());
		k.module->outputs[0].setVoltage((*newSink)[3]);
	}
};
std::mutex injectedMutex;
struct InjectMutexKernel : Kernel {
	void process(const KernelContext& k) override {
		std::lock_guard<std::mutex> lock(injectedMutex);
		k.module->outputs[0].setVoltage(k.module->inputs[0].getVoltage());
	}
};
int devNull = -1;
struct InjectWriteKernel : Kernel {
	void process(const KernelContext& k) override {
		char c = 'x';
		if (::write(devNull, &c, 1) < 0)
			k.module->outputs[0].setVoltage(-1.f);
		k.module->outputs[0].setVoltage(k.module->inputs[0].getVoltage());
	}
};

// --- workloads ------------------------------------------------------------------------

struct Variant {
	std::string workload;
	std::string variant;
	int channels;
	std::function<std::unique_ptr<engine::Module>()> make;
};

std::vector<Variant> variants() {
	using M = std::unique_ptr<engine::Module>;
	auto rp = [](std::vector<float> defaults, Kernel* k) {
		return M(new RackPyModule(std::move(defaults), std::unique_ptr<Kernel>(k)));
	};
	return {
		{"trivial", "native", 1, [] { return M(new NativeTrivial); }},
		{"trivial", "rackpy", 1, [=] { return rp({}, new TrivialKernel); }},
		{"gain", "native", 1, [] { return M(new NativeGain); }},
		{"gain", "rackpy", 1, [=] { return rp({0.8f}, new GainKernel); }},
		{"sine", "native", 1, [] { return M(new NativeSine); }},
		{"sine", "rackpy", 1, [=] { return rp({0.f}, new SineKernel); }},
		{"poly16", "native", 16, [] { return M(new NativePoly); }},
		{"poly16", "rackpy", 16, [=] { return rp({}, new PolyKernel); }},
		{"poly16_simd", "native", 16, [] { return M(new NativePolySimd); }},
		{"poly16_simd", "rackpy", 16, [=] { return rp({}, new PolySimdKernel); }},
		{"chain", "native", 1, [] { return M(new NativeChain); }},
		{"chain", "rackpy_plan", 1, [=] { return rp({0.f, 0.8f}, new PlanKernel); }},
		{"chain", "rackpy_plan_static", 1,
			[=] { return rp({0.f, 0.8f}, new StaticPlanKernel<StaticSine, StaticGain, StaticOutput>); }},
	};
}

std::vector<Variant> injected() {
	using M = std::unique_ptr<engine::Module>;
	auto rp = [](Kernel* k) { return M(new RackPyModule({}, std::unique_ptr<Kernel>(k))); };
	return {
		{"inject", "malloc", 1, [=] { return rp(new InjectMallocKernel); }},
		{"inject", "new", 1, [=] { return rp(new InjectNewKernel); }},
		{"inject", "mutex", 1, [=] { return rp(new InjectMutexKernel); }},
		{"inject", "write", 1, [=] { return rp(new InjectWriteKernel); }},
	};
}

struct Driver {
	engine::Module::ProcessArgs args{};
	int64_t frame = 0;
	Driver() {
		args.sampleRate = SAMPLE_RATE;
		args.sampleTime = 1.f / SAMPLE_RATE;
	}
	/** One frame, as Rack's engine does it: set inputs, call process() once. */
	float step(engine::Module& m, int channels) {
		float v = pitchTable[frame % INPUT_TABLE];
		auto& in = m.inputs[0];
		in.setChannels(channels);
		for (int c = 0; c < channels; c++)
			in.setVoltage(v + 0.01f * c, c);
		args.frame = frame++;
		m.process(args);
		return m.outputs[0].getVoltage(0);
	}
};

using GuardFn = void (*)();
using ReportFn = void (*)(char*, size_t);
GuardFn guardEnter = nullptr;
GuardFn guardLeave = nullptr;
GuardFn guardReset = nullptr;
ReportFn guardReport = nullptr;

void loadGuard() {
	guardEnter = (GuardFn) dlsym(RTLD_DEFAULT, "audio_guard_enter");
	guardLeave = (GuardFn) dlsym(RTLD_DEFAULT, "audio_guard_leave");
	guardReset = (GuardFn) dlsym(RTLD_DEFAULT, "audio_guard_reset");
	guardReport = (ReportFn) dlsym(RTLD_DEFAULT, "audio_guard_report");
}

double percentile(std::vector<double> v, double p) {
	std::sort(v.begin(), v.end());
	size_t i = std::min(v.size() - 1, (size_t) (p * (v.size() - 1) + 0.5));
	return v[i];
}

int timeMode(int reps, int blocks, unsigned seed) {
	auto all = variants();
	std::vector<std::unique_ptr<engine::Module>> modules;
	std::vector<Driver> drivers(all.size());
	for (auto& v : all)
		modules.push_back(v.make());
	std::vector<std::vector<double>> p50(all.size()), p99(all.size()), mean(all.size());
	std::vector<double> checksum(all.size(), 0.0);
	std::vector<size_t> order(all.size());
	for (size_t i = 0; i < order.size(); i++)
		order[i] = i;
	std::mt19937 rng(seed);
	std::vector<double> blockNs(blocks);
	for (int r = 0; r < reps; r++) {
		std::shuffle(order.begin(), order.end(), rng);
		for (size_t i : order) {
			auto& m = *modules[i];
			int ch = all[i].channels;
			for (int w = 0; w < 50 * BLOCK; w++)  // warm caches and branch predictors for this variant
				checksum[i] += drivers[i].step(m, ch);
			for (int b = 0; b < blocks; b++) {
				auto t0 = std::chrono::steady_clock::now();
				float acc = 0.f;
				for (int f = 0; f < BLOCK; f++)
					acc += drivers[i].step(m, ch);
				auto t1 = std::chrono::steady_clock::now();
				checksum[i] += acc;
				blockNs[b] = std::chrono::duration<double, std::nano>(t1 - t0).count() / BLOCK;
			}
			p50[i].push_back(percentile(blockNs, 0.50));
			p99[i].push_back(percentile(blockNs, 0.99));
			double sum = 0;
			for (double x : blockNs)
				sum += x;
			mean[i].push_back(sum / blocks);
		}
	}
	auto arr = [](const std::vector<double>& v) {
		std::string s = "[";
		for (size_t i = 0; i < v.size(); i++)
			s += string::f("%s%.3f", i ? ", " : "", v[i]);
		return s + "]";
	};
	std::printf("{\"unit\": \"ns per frame (per process() call)\", \"block_frames\": %d, \"blocks_per_rep\": %d, "
		"\"reps\": %d, \"seed\": %u, \"sample_rate\": %g, \"results\": [\n",
		BLOCK, blocks, reps, seed, SAMPLE_RATE);
	for (size_t i = 0; i < all.size(); i++) {
		std::printf("  {\"workload\": \"%s\", \"variant\": \"%s\", \"channels\": %d, \"checksum\": %.6g, "
			"\"p50_per_rep\": %s, \"p99_per_rep\": %s, \"mean_per_rep\": %s}%s\n",
			all[i].workload.c_str(), all[i].variant.c_str(), all[i].channels, checksum[i], arr(p50[i]).c_str(),
			arr(p99[i]).c_str(), arr(mean[i]).c_str(), i + 1 < all.size() ? "," : "");
	}
	std::printf("]}\n");
	return 0;
}

int equivalenceMode(int frames) {
	auto all = variants();
	std::printf("{\"frames\": %d, \"pairs\": [\n", frames);
	bool first = true;
	for (size_t j = 0; j < all.size(); j++) {
		if (all[j].variant == "native")
			continue;
		size_t i = 0;
		while (all[i].workload != all[j].workload || all[i].variant != "native")
			i++;
		auto a = all[i].make();
		auto b = all[j].make();
		Driver da, db;
		int ch = all[i].channels;
		int64_t mismatches = 0;
		double maxAbs = 0.0;
		int64_t maxUlp = 0;
		for (int f = 0; f < frames; f++) {
			da.step(*a, ch);
			db.step(*b, ch);
			const float* x = a->outputs[0].getVoltages();
			const float* y = b->outputs[0].getVoltages();
			if (std::memcmp(x, y, sizeof(float) * ch) != 0)
				mismatches++;
			for (int c = 0; c < ch; c++) {
				maxAbs = std::max(maxAbs, (double) std::fabs(x[c] - y[c]));
				int32_t ix, iy;
				std::memcpy(&ix, &x[c], 4);
				std::memcpy(&iy, &y[c], 4);
				maxUlp = std::max<int64_t>(maxUlp, std::llabs((int64_t) ix - (int64_t) iy));
			}
		}
		std::printf("%s  {\"workload\": \"%s\", \"a\": \"%s\", \"b\": \"%s\", \"frames_with_any_bit_difference\": %lld, "
			"\"max_abs_difference_volts\": %.3g, \"max_ulp_difference\": %lld}\n",
			first ? "" : ",", all[i].workload.c_str(), all[i].variant.c_str(), all[j].variant.c_str(),
			(long long) mismatches, maxAbs, (long long) maxUlp);
		first = false;
	}
	std::printf("]}\n");
	return 0;
}

int guardMode(int frames) {
	if (!guardEnter || !guardReport) {
		std::fprintf(stderr, "guard mode needs LD_PRELOAD=audio_guard.so\n");
		return 2;
	}
	devNull = ::open("/dev/null", O_WRONLY);
	auto run = [&](const Variant& v) {
		auto m = v.make();
		Driver d;
		for (int f = 0; f < 1000; f++)
			d.step(*m, v.channels);  // first calls may allocate lazily outside the measured path
		guardReset();
		guardEnter();
		for (int f = 0; f < frames; f++)
			d.step(*m, v.channels);
		guardLeave();
		char buf[512];
		guardReport(buf, sizeof(buf));
		return std::string(buf);
	};
	std::printf("{\"frames\": %d, \"runs\": [\n", frames);
	auto prod = variants();
	auto inj = injected();
	std::vector<Variant> both(prod);
	both.insert(both.end(), inj.begin(), inj.end());
	for (size_t i = 0; i < both.size(); i++) {
		std::printf("  {\"workload\": \"%s\", \"variant\": \"%s\", \"events\": %s}%s\n", both[i].workload.c_str(),
			both[i].variant.c_str(), run(both[i]).c_str(), i + 1 < both.size() ? "," : "");
	}
	std::printf("]}\n");
	return 0;
}

}  // namespace

int main(int argc, char** argv) {
	for (int i = 0; i < INPUT_TABLE; i++)
		pitchTable[i] = 0.5f * std::sin(i * 0.01f);  // deterministic modulating pitch CV
	loadGuard();
	std::string mode = argc > 1 ? argv[1] : "time";
	if (mode == "time")
		return timeMode(argc > 2 ? std::atoi(argv[2]) : 21, argc > 3 ? std::atoi(argv[3]) : 400, 20261021);
	if (mode == "equivalence")
		return equivalenceMode(200000);
	if (mode == "guard")
		return guardMode(100000);
	std::fprintf(stderr, "usage: bench time|equivalence|guard\n");
	return 2;
}
