// SPDX-License-Identifier: GPL-3.0-or-later
// RP-36: the example plugins' kernel libraries under the audio guard. Run with
// LD_PRELOAD=audio_guard.so and the libraries' paths as arguments. Each library is
// dlopen'ed and registered as RackPy's loader registers it; then each of its kernel types
// processes 20 000 frames of polyphonic input (0 to 16 channels, changing) on a plain
// Rack module, with the module's controls moving, inside the guard. A kernel that reads a
// neighbour gets one: a module of the same plugin's "Source" model, so its reading path
// runs too. Prints, as JSON, each type's counts of heap, lock, write and GIL calls.
#include <rack.hpp>
#include <rackpy/kernel.hpp>

#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using GuardFn = void (*)();
using ReportFn = void (*)(char*, size_t);
GuardFn enter, leave, resetCounts;
ReportFn report;

struct Collect : rackpy::KernelRegistrar {
	std::vector<rackpy::KernelType> types;
	void add(rackpy::KernelType type) override { types.push_back(std::move(type)); }
};

std::string run(const rackpy::KernelType& type, rack::plugin::Model& own, rack::engine::Module& neighbour) {
	rack::engine::Module m;
	m.config(8, 8, 8, 8);
	m.model = &own;
	m.leftExpander.module = &neighbour;
	m.leftExpander.moduleId = 1;
	for (auto& o : m.outputs)
		o.channels = 1;  // patched
	std::vector<double> values;
	for (const auto& field : type.fields)
		values.push_back(field.defaultValue);
	std::unique_ptr<rackpy::Kernel> kernel = type.create(values);
	kernel->sampleRateChange(48000.f, 1.f / 48000.f);
	rack::engine::Module::ProcessArgs args;
	args.sampleRate = 48000.f;
	args.sampleTime = 1.f / 48000.f;
	resetCounts();
	for (int n = 0; n < 20000; n++) {
		args.frame = n;
		int channels = (n / 1000) % 17;  // 0 to 16, changing every 1000 frames
		for (auto& in : m.inputs) {
			in.channels = (uint8_t) channels;
			for (int c = 0; c < channels; c++)
				in.voltages[c] = 2.f * std::sin(0.01f * n + c);
		}
		for (int p = 0; p < 8; p++)
			m.params[p].setValue((float) ((n / 700 + p) % 4));
		neighbour.params[0].setValue((float) (n % 100) / 100.f);
		neighbour.outputs[0].setVoltage(std::sin(0.02f * n));
		enter();
		bool ok = kernel->process(m, args);
		if (n == 10000) {
			kernel->reset();
			kernel->sampleRateChange(96000.f, 1.f / 96000.f);
		}
		leave();
		if (!ok)
			return "{\"failed\": true}";
	}
	char text[512];
	report(text, sizeof text);
	return text;
}

}  // namespace

int main(int argc, char** argv) {
	enter = (GuardFn) dlsym(RTLD_DEFAULT, "audio_guard_enter");
	leave = (GuardFn) dlsym(RTLD_DEFAULT, "audio_guard_leave");
	resetCounts = (GuardFn) dlsym(RTLD_DEFAULT, "audio_guard_reset");
	report = (ReportFn) dlsym(RTLD_DEFAULT, "audio_guard_report");
	if (!enter || !leave || !resetCounts || !report) {
		std::fprintf(stderr, "rackpy_example_kernel_guard needs LD_PRELOAD=audio_guard.so\n");
		return 2;
	}
	rack::plugin::Plugin plugin;
	rack::plugin::Model own, source;
	own.plugin = source.plugin = &plugin;
	own.slug = "Example";
	source.slug = "Source";
	rack::engine::Module neighbour;
	neighbour.config(8, 8, 8, 8);
	neighbour.model = &source;
	neighbour.outputs[0].channels = 1;
	std::printf("{");
	int printed = 0;
	for (int i = 1; i < argc; i++) {
		void* library = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL);
		auto registerKernels = library ? (void (*)(rackpy::KernelRegistrar&)) dlsym(library, "rackpy_register_kernels") : nullptr;
		auto ns = library ? (const char* (*) ()) dlsym(library, "rackpy_kernel_namespace") : nullptr;
		if (!registerKernels || !ns) {
			std::fprintf(stderr, "cannot load %s: %s\n", argv[i], dlerror());
			return 2;
		}
		Collect collect;
		registerKernels(collect);
		for (const auto& type : collect.types)
			std::printf("%s\"%s.%s\": %s", printed++ ? ", " : "", ns(), type.name.c_str(), run(type, own, neighbour).c_str());
	}
	std::printf("}\n");
	return 0;
}
