// SPDX-License-Identifier: GPL-3.0-or-later
// C++ reference for examples/polyphony (RP-36): a polyphonic sine oscillator as a C++
// plugin author writes it in process(), on Rack's own Param, Input and Output, a channel
// at a time and four channels at a time with float_4. Compiled with the SDK's flags and
// run in Rack's floating-point mode. It prints, as JSON, scenarios of V/OCT channel counts
// (unpatched, mono, a partial SIMD group, sixteen, and a count that changes mid-stream)
// with every frame's input voltages and both versions' outputs; tests/examples replays
// the inputs on the example's modules inside Rack and compares.
#include <rack.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace rack;
using simd::float_4;

namespace {

struct Ports {
	engine::Param pitch;
	engine::Input voct;
	engine::Output sine;
};

struct ScalarOscillator : Ports {
	float phase[PORT_MAX_CHANNELS] = {};
	void process(float sampleTime) {
		int channels = std::max(1, voct.getChannels());
		for (int c = 0; c < channels; c++) {
			float freq = dsp::FREQ_C4 * std::pow(2.f, pitch.getValue() + voct.getPolyVoltage(c));
			phase[c] += freq * sampleTime;
			phase[c] -= std::floor(phase[c]);
			sine.setVoltage(5.f * std::sin(2.f * float(M_PI) * phase[c]), c);
		}
		sine.setChannels(channels);
	}
};

struct SimdOscillator : Ports {
	float_4 phase[PORT_MAX_CHANNELS / 4] = {};
	void process(float sampleTime) {
		int channels = std::max(1, voct.getChannels());
		for (int c = 0; c < channels; c += 4) {
			float_4 freq = dsp::FREQ_C4 * simd::pow(2.f, pitch.getValue() + voct.getPolyVoltageSimd<float_4>(c));
			phase[c / 4] += freq * sampleTime;
			phase[c / 4] -= simd::floor(phase[c / 4]);
			sine.setVoltageSimd(5.f * simd::sin(2.f * float(M_PI) * phase[c / 4]), c);
		}
		sine.setChannels(channels);
	}
};

std::string num(float x) {
	char buf[32];
	std::snprintf(buf, sizeof buf, "%.9g", x);
	return buf;
}

std::string channels(const float* v, int n) {
	std::string out = "[";
	for (int c = 0; c < n; c++)
		out += (c ? ", " : "") + num(v[c]);
	return out + "]";
}

struct Scenario {
	const char* name;
	float knob;
	int first, second;  // V/OCT channels before and after the halfway frame
};

/** Each channel's V/OCT: spread over two octaves, with a slow vibrato. */
float voltage(int frame, int c) {
	return (c - 7.5f) / 6.f + 0.1f * std::sin(2.f * float(M_PI) * frame / 480.f);
}

}  // namespace

int main() {
	system::resetFpuFlags();  // Rack's floating-point mode: flush-to-zero, denormals-are-zero
	const float rate = 48000.f;
	const int frames = 1200;
	const std::vector<Scenario> scenarios = {
		{"unpatched", 0.5f, 0, 0},
		{"mono", -1.f, 1, 1},
		{"five voices", 0.f, 5, 5},
		{"sixteen voices", 0.25f, 16, 16},
		{"sixteen, then three", 0.f, 16, 3},
	};
	std::printf("{\"rate\": %s, \"scenarios\": [", num(rate).c_str());
	for (size_t i = 0; i < scenarios.size(); i++) {
		const Scenario& s = scenarios[i];
		ScalarOscillator scalar;
		SimdOscillator simd4;
		scalar.pitch.setValue(s.knob);
		simd4.pitch.setValue(s.knob);
		scalar.sine.channels = simd4.sine.channels = 1;  // a patched output
		std::string inputs = "[", scalarOut = "[", simdOut = "[";
		// Rack's engine computes args.sampleTime once and modules receive the stored value; volatile keeps
		// the compiler from turning x * (1 / rate) into x / rate after inlining, which rounds differently.
		volatile float sampleTime = 1.f / rate;
		for (int k = 0; k < frames; k++) {
			int n = k < frames / 2 ? s.first : s.second;
			for (Ports* p : {(Ports*) &scalar, (Ports*) &simd4}) {
				p->voct.channels = (uint8_t) n;  // as Rack sets a patched input's channels
				for (int c = 0; c < n; c++)
					p->voct.setVoltage(voltage(k, c), c);
			}
			scalar.process(sampleTime);
			simd4.process(sampleTime);
			const char* sep = k ? ", " : "";
			inputs += sep + channels(scalar.voct.getVoltages(), n);
			scalarOut += sep + channels(scalar.sine.getVoltages(), scalar.sine.getChannels());
			simdOut += sep + channels(simd4.sine.getVoltages(), simd4.sine.getChannels());
		}
		std::printf("%s{\"name\": \"%s\", \"knob\": %s, \"inputs\": %s], \"scalar\": %s], \"simd\": %s]}", i ? ", " : "",
			s.name, num(s.knob).c_str(), inputs.c_str(), scalarOut.c_str(), simdOut.c_str());
	}
	std::printf("]}\n");
	return 0;
}
