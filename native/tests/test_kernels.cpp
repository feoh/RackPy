// SPDX-License-Identifier: GPL-3.0-or-later
// RP-17A: the kernel runtime without Python or a Rack host. The built-in
// kernels are compared sample for sample with the same arithmetic written
// directly (the RP-07 reference), and the slot's engine/main-thread
// handover is driven step by step on one thread.
#include "../module/module.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "rackpy/runtime/diagnostics.hpp"

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
	return false;
}

std::unique_ptr<dsp::Hosted> make(const char* name, std::vector<double> values) {
	auto hosted = std::make_unique<dsp::Hosted>();
	hosted->type = dsp::findType(name);
	hosted->kernel = hosted->type->create(values);
	return hosted;
}

rack::engine::Module::ProcessArgs frame(float sampleRate, int64_t n) {
	rack::engine::Module::ProcessArgs args;
	args.sampleRate = sampleRate;
	args.sampleTime = 1.f / sampleRate;
	args.frame = n;
	return args;
}

/** As Rack does when a cable is plugged in: an unconnected port keeps 0 channels whatever setChannels asks. */
void connect(rack::engine::Port& port, int channels = 1) {
	port.channels = (uint8_t) channels;
}

bool same(float a, float b) {
	return std::memcmp(&a, &b, sizeof a) == 0;
}

/** A kernel that fails on its third frame and counts what reaches it. */
struct Failing : Kernel {
	int frames = 0, resets = 0, rates = 0;
	double control = 0;
	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs&) noexcept override {
		m.outputs[0].setVoltage(1.f);
		return ++frames < 3;
	}
	void reset() noexcept override { resets++; }
	void sampleRateChange(float, float) noexcept override { rates++; }
	void setControl(int, double value) noexcept override { control = value; }
};

void testGainMatchesDirectCpp() {
	module::Module m;
	m.config(1, 1, 1, 0);
	m.kernels.attach(make("rackpy.Gain", {0, 0, 0, 1}), false);
	connect(m.inputs[0]);
	connect(m.outputs[0]);
	uint32_t seed = 1;
	for (int64_t n = 0; n < 100000; n++) {
		int channels = 1 + (int) (n % 16);
		m.inputs[0].setChannels(channels);
		for (int c = 0; c < channels; c++) {
			seed = seed * 1664525u + 1013904223u;
			m.inputs[0].setVoltage(((float) (seed >> 8) / 16777216.f - 0.5f) * 20.f, c);
		}
		m.params[0].setValue((float) (n % 101) / 100.f);
		m.process(frame(48000.f, n));
		CHECK(m.outputs[0].getChannels() == channels);
		for (int c = 0; c < channels; c++)
			if (!same(m.outputs[0].getVoltage(c), m.inputs[0].getVoltage(c) * (m.params[0].getValue() * 1.f))) {
				CHECK(!"gain differs from direct C++");
				return;
			}
	}
}

void testSineMatchesDirectCpp() {
	for (int channels : {1, 4, 16}) {
		module::Module m;
		m.config(1, 1, 1, 0);
		m.kernels.attach(make("rackpy.Sine", {0, 0, 0, 5}), false);
		connect(m.inputs[0], channels);
		connect(m.outputs[0]);
		float phase[16] = {};
		for (int64_t n = 0; n < 100000; n++) {
			for (int c = 0; c < channels; c++)
				m.inputs[0].setVoltage((float) ((n / 1000 + c) % 9) / 4.f - 1.f, c);
			m.params[0].setValue(0.25f);
			m.process(frame(44100.f, n));
			for (int c = 0; c < channels; c++) {
				// RP-07's reference oscillator (benchmarks/spike/src/dsp.hpp), written out.
				float pitch = m.inputs[0].getPolyVoltage(c) + m.params[0].getValue();
				float freq = rack::dsp::FREQ_C4 * std::pow(2.f, pitch);
				phase[c] += freq * (1.f / 44100.f);
				if (phase[c] >= 1.f)
					phase[c] -= 1.f;
				float expected = 5.f * std::sin(2.f * float(M_PI) * phase[c]);
				if (!same(m.outputs[0].getVoltage(c), expected)) {
					CHECK(!"sine differs from direct C++");
					return;
				}
			}
		}
	}
}

void testWiringChecks() {
	module::Module m;
	m.config(1, 1, 1, 1);
	const KernelType& gain = *dsp::findType("rackpy.Gain");
	dsp::checkWiring(gain, {0, 0, -1, 2}, m);
	CHECK(throwsInvalid([&] { dsp::checkWiring(gain, {1, 0, -1, 1}, m); }, "rackpy.Gain.input = 1, but the module has 1 inputs"));
	CHECK(throwsInvalid([&] { dsp::checkWiring(gain, {-1, 0, -1, 1}, m); }, "rackpy.Gain.input must name one of the module's inputs"));
	CHECK(throwsInvalid([&] { dsp::checkWiring(gain, {0, 0.5, -1, 1}, m); }, "whole number"));
	CHECK(throwsInvalid([&] { dsp::checkWiring(gain, {0, 0, -2, 1}, m); }, "rackpy.Gain.param must name"));
	CHECK(throwsInvalid([&] { dsp::checkWiring(gain, {0, 0, -1, NAN}, m); }, "must be finite"));
	CHECK(throwsInvalid([&] { dsp::checkWiring(gain, {0, 0}, m); }, "needs 4 field values"));
}

void testHandoverControlsAndReclaim() {
	module::Module m;
	m.config(0, 0, 1, 0);
	connect(m.outputs[0]);
	auto first = std::make_unique<dsp::Hosted>();
	auto* a = new Failing;
	first->kernel.reset(a);
	auto token = first->token;
	dsp::Hosted* hostedA = first.get();
	m.kernels.attach(std::move(first), true);  // as when Rack's engine runs the module
	CHECK(m.kernels.latest() == hostedA && a->frames == 0);
	CHECK(m.kernels.set(hostedA, 0, 3.5, true) && a->control == 3.5);  // still waiting: applied directly
	m.process(frame(48000.f, 0));
	CHECK(a->frames == 1 && a->rates == 1);  // picked up, told the sample rate first
	CHECK(m.kernels.set(hostedA, 0, 4.5, true) && a->control == 3.5);  // queued now
	m.process(frame(48000.f, 1));
	CHECK(a->control == 4.5 && a->frames == 2);
	// The bounded queue refuses rather than blocks.
	size_t accepted = 0;
	while (m.kernels.set(hostedA, 0, 1.0, true))
		accepted++;
	CHECK(accepted == dsp::CONTROL_CAPACITY && m.kernels.queued() == dsp::CONTROL_CAPACITY);
	m.process(frame(48000.f, 2));  // drains the queue; this is the failing frame
	CHECK(m.kernels.queued() == 0 && m.kernels.failed());
	CHECK(m.outputs[0].getVoltage() == 0.f);  // silenced
	CHECK(runtime::diagnosticCount(runtime::DiagnosticCode::KernelFailed) == 1);
	m.process(frame(48000.f, 3));
	CHECK(a->frames == 3 && m.outputs[0].getVoltage() == 0.f);  // not called again
	m.kernels.reset();
	CHECK(!m.kernels.failed() && a->resets == 1);  // Initialize clears the failure

	// Replacement: the engine retires the old kernel; the main thread deletes it.
	auto second = std::make_unique<dsp::Hosted>();
	auto* b = new Failing;
	second->kernel.reset(b);
	m.kernels.attach(std::move(second), true);
	CHECK(token->alive());  // the engine has not switched yet
	m.process(frame(48000.f, 4));
	CHECK(b->frames == 1 && token->alive());  // switched; the old kernel waits for the main thread
	m.kernels.reclaim();
	CHECK(!token->alive());
}

void testReplacingAWaitingKernelDeletesIt() {
	module::Module m;
	m.config(0, 0, 1, 0);
	auto one = std::make_unique<dsp::Hosted>();
	one->kernel = std::make_unique<Failing>();
	auto token = one->token;
	m.kernels.attach(std::move(one), true);
	auto two = std::make_unique<dsp::Hosted>();
	two->kernel = std::make_unique<Failing>();
	m.kernels.attach(std::move(two), true);
	CHECK(!token->alive());  // never reached the engine, so the main thread deleted it
}

}  // namespace

int main() {
	runtime::markMainThread();
	dsp::registerBuiltins();
	dsp::registerBuiltins();  // idempotent
	CHECK(dsp::types().size() == 2);
	testGainMatchesDirectCpp();
	testSineMatchesDirectCpp();
	testWiringChecks();
	testHandoverControlsAndReclaim();
	testReplacingAWaitingKernelDeletesIt();
	if (failures)
		std::fprintf(stderr, "%d check(s) failed\n", failures);
	else
		std::printf("kernel runtime: all checks passed\n");
	return failures ? 1 : 0;
}
