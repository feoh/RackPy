// SPDX-License-Identifier: GPL-3.0-or-later
// Native node types for DSP graphs (RP-21). The plan interpreter (plan.cpp)
// dispatches to these, and the compiled tier (RP-21C) composes them
// statically, so both tiers share one implementation of each node.
//
// Engine-thread code: no allocation, locks, IO, logging, exceptions or
// Python. Semantics are documented in docs/how-to/graphs.md and mirrored by
// rackpy.dsp.graph_reference.
#pragma once
#include <rack.hpp>

#include <algorithm>
#include <cmath>

namespace rackpy {
namespace graph {

constexpr int MAX_CHANNELS = rack::engine::PORT_MAX_CHANNELS;

/** One frame of a signal: 0 to 16 channels. Values at or above `channels` are never read. */
struct Signal {
	int channels = 0;
	float v[MAX_CHANNELS] = {};
};

/** Channel c of an operand: a mono signal applies to every channel, and channels a shorter one lacks read 0 V. */
inline float lane(const Signal& s, int c) {
	return s.channels == 1 ? s.v[0] : (c < s.channels ? s.v[c] : 0.f);
}

inline void mono(Signal& out, float x) {
	out.channels = 1;
	out.v[0] = x;
}

template <typename F>
inline void map1(Signal& out, const Signal& a, F f) {
	out.channels = a.channels;
	for (int c = 0; c < a.channels; c++)
		out.v[c] = f(a.v[c]);
}

template <typename F>
inline void map2(Signal& out, const Signal& a, const Signal& b, F f) {
	int n = std::max(a.channels, b.channels);
	out.channels = n;
	for (int c = 0; c < n; c++)
		out.v[c] = f(lane(a, c), lane(b, c));
}

template <typename F>
inline void map3(Signal& out, const Signal& a, const Signal& b, const Signal& d, F f) {
	int n = std::max(a.channels, std::max(b.channels, d.channels));
	out.channels = n;
	for (int c = 0; c < n; c++)
		out.v[c] = f(lane(a, c), lane(b, c), lane(d, c));
}

// --- Stateless functions, one per node type -------------------------------------------
inline float add(float a, float b) { return a + b; }
inline float sub(float a, float b) { return a - b; }
inline float mul(float a, float b) { return a * b; }
inline float div(float a, float b) { return a / b; }
inline float fmin(float a, float b) { return std::fmin(a, b); }
inline float fmax(float a, float b) { return std::fmax(a, b); }
inline float lt(float a, float b) { return a < b ? 1.f : 0.f; }
inline float le(float a, float b) { return a <= b ? 1.f : 0.f; }
inline float gt(float a, float b) { return a > b ? 1.f : 0.f; }
inline float ge(float a, float b) { return a >= b ? 1.f : 0.f; }
inline float neg(float x) { return -x; }
inline float fabs(float x) { return std::fabs(x); }
inline float clamp(float x, float low, float high) { return rack::math::clamp(x, low, high); }
inline float where(float condition, float a, float b) { return condition > 0.f ? a : b; }
/** As Rack's tutorial oscillator and rackpy.Sine compute it. */
inline float pitchToFreq(float v) { return rack::dsp::FREQ_C4 * std::pow(2.f, v); }
inline float sin2pi(float x) { return std::sin(2.f * float(M_PI) * x); }

// --- Ports, parameters and lights ---------------------------------------------------------
inline void readInput(Signal& out, rack::engine::Input& in) {
	int n = in.getChannels();
	out.channels = n;
	for (int c = 0; c < n; c++)
		out.v[c] = in.getVoltage(c);
}

/** At least one channel, as Port::setChannels gives a patched output. */
inline void writeOutput(rack::engine::Output& out, const Signal& s) {
	int n = std::max(1, s.channels);
	out.setChannels(n);
	for (int c = 0; c < n; c++)
		out.setVoltage(lane(s, c), c);
}

inline void writeLight(rack::engine::Light& light, const Signal& s) {
	light.setBrightness(s.channels > 0 ? s.v[0] : 0.f);
}

// --- Stateful nodes -------------------------------------------------------------------------
/** A phase from 0 to 1 per channel (at least one), kept as Rack's tutorial oscillator keeps it. */
struct Phasor {
	float phase[MAX_CHANNELS] = {};
	void reset() { std::fill(phase, phase + MAX_CHANNELS, 0.f); }
	void process(Signal& out, const Signal& freq, float sampleTime) {
		int n = std::max(1, freq.channels);
		out.channels = n;
		for (int c = 0; c < n; c++) {
			phase[c] += lane(freq, c) * sampleTime;
			if (phase[c] >= 1.f)
				phase[c] -= 1.f;
			out.v[c] = phase[c];
		}
	}
};

/** Rack's dsp::SchmittTrigger per channel (at least one): 1 on the frame the input rises to `high`. */
struct Schmitt {
	rack::dsp::SchmittTrigger trigger[MAX_CHANNELS];
	float low = 0.f, high = 1.f;
	void reset() {
		for (auto& t : trigger)
			t.reset();
	}
	void process(Signal& out, const Signal& x) {
		int n = std::max(1, x.channels);
		out.channels = n;
		for (int c = 0; c < n; c++)
			out.v[c] = trigger[c].process(lane(x, c), low, high) ? 1.f : 0.f;
	}
};

/** Rack's dsp::PulseGenerator per channel (at least one): a trigger above 0 starts a pulse of `duration` seconds. */
struct Pulse {
	rack::dsp::PulseGenerator pulse[MAX_CHANNELS];
	float duration = 1e-3f;
	void reset() {
		for (auto& p : pulse)
			p.reset();
	}
	void process(Signal& out, const Signal& trigger, float sampleTime) {
		int n = std::max(1, trigger.channels);
		out.channels = n;
		for (int c = 0; c < n; c++) {
			if (lane(trigger, c) > 0.f)
				pulse[c].trigger(duration);
			out.v[c] = pulse[c].process(sampleTime) ? 1.f : 0.f;
		}
	}
};

/** One sample of delay: what it was fed at the end of the previous frame (one mono channel of `initial` at first). */
struct Delay {
	Signal held;
	float initial = 0.f;
	void reset() { mono(held, initial); }
	void process(Signal& out) const { out = held; }
	void latch(const Signal& source) { held = source; }
};

}  // namespace graph
}  // namespace rackpy
