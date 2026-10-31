// SPDX-License-Identifier: GPL-3.0-or-later
// C++ reference for examples/oscillator (RP-33): the sine oscillator with a
// 1 Hz blink as a C++ plugin author writes it, on Rack's own Param, Input,
// Output and Light, compiled with the SDK's flags and run in Rack's
// floating-point mode. It keeps the oscillator's phase in [-0.5, 0.5), as the
// common C++ form does; RackPy's graph phasor keeps it in [0, 1), so the two
// agree to rounding, and tests/examples measures by how much.
//
// stdin: "<sample rate> <frames> <patched: 0 or 1>", then one line per frame:
// "<pitch knob volts> <V/oct input volts>". stdout: one line per frame,
// "<sine volts> <light brightness>", each with enough digits to round-trip a float.
#include <rack.hpp>

#include <cmath>
#include <cstdio>

using namespace rack;

namespace {

struct Oscillator {
	engine::Param pitch;
	engine::Input voct;
	engine::Output sine;
	engine::Light blink;
	float phase = 0.f;  // in cycles, [-0.5, 0.5)
	float blinkPhase = 0.f;  // in seconds, [0, 1)

	void process(float sampleTime) {
		float volts = pitch.getValue() + voct.getVoltage();  // 0 V is C4; 1 V per octave
		float freq = dsp::FREQ_C4 * std::pow(2.f, volts);
		phase += freq * sampleTime;
		if (phase >= 0.5f)
			phase -= 1.f;
		sine.setVoltage(5.f * std::sin(2.f * float(M_PI) * phase));
		blinkPhase += sampleTime;
		if (blinkPhase >= 1.f)
			blinkPhase -= 1.f;
		blink.setBrightness(blinkPhase < 0.5f ? 1.f : 0.f);
	}
};

}  // namespace

int main() {
	system::resetFpuFlags();  // Rack's floating-point mode: flush-to-zero, denormals-are-zero
	float rate = 0.f;
	long frames = 0;
	int patched = 0;
	if (std::scanf("%f %ld %d", &rate, &frames, &patched) != 3 || rate <= 0.f || frames < 0) {
		std::fprintf(stderr, "expected: <sample rate> <frames> <patched>\n");
		return 2;
	}
	Oscillator osc;
	osc.voct.setChannels(patched ? 1 : 0);
	// Rack's engine computes args.sampleTime once and modules receive the stored value; volatile keeps
	// the compiler from turning x * (1 / rate) into x / rate after inlining, which rounds differently.
	volatile float sampleTime = 1.f / rate;
	for (long k = 0; k < frames; k++) {
		float knob = 0.f, volts = 0.f;
		if (std::scanf("%f %f", &knob, &volts) != 2) {
			std::fprintf(stderr, "frame %ld: expected <knob> <input>\n", k);
			return 2;
		}
		osc.pitch.setValue(knob);
		if (patched)
			osc.voct.setVoltage(volts);
		osc.process(sampleTime);
		std::printf("%.9g %.9g\n", osc.sine.getVoltage(), osc.blink.getBrightness());
	}
	return 0;
}
