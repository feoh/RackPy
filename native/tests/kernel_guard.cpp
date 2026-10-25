// SPDX-License-Identifier: GPL-3.0-or-later
// RP-17A: the kernel path under the RP-07 audio-path guard. Run with
// LD_PRELOAD=audio_guard.so: every engine-thread call (process, with control
// draining, kernel pickup, retirement and failure) is marked, and the guard
// counts heap, lock, write and GIL calls inside the marks. Main-thread work
// (attach, set, reclaim) stays outside them, as in Rack. Prints JSON.
#include "../module/module.hpp"

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace rackpy;

namespace {

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
	std::printf("{\"frames\": %d, \"production\": %s, \"failure\": %s, \"inject\": %s}\n", frames, production.c_str(),
		failure.c_str(), inject.c_str());
	return 0;
}
