// SPDX-License-Identifier: GPL-3.0-or-later
// RackPy DSP graph nodes (RP-21): the native node types that Python graphs
// (rackpy.dsp.graph) are built from, and the interface for adding more.
//
// RackPy's plan interpreter dispatches to these functions and structs, and
// the C++ that RackPy generates from a graph (the compiled tier) composes
// them statically, so every tier computes each node the same way. Kernel
// libraries (RP-17B) include this header to add native node types of their
// own (RACKPY_GRAPH_NODES below). Include it after <rack.hpp>; it uses only
// Rack's public API.
//
// Engine-thread code: no allocation, locks, IO, logging, exceptions or
// Python. Semantics are documented in docs/how-to/graphs.md and mirrored by
// rackpy.dsp.graph_reference.
#pragma once
#include <rack.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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

/** Makes a node's values opaque to the optimizer. The compiled tier calls it on every node's signal, so
that -funsafe-math-optimizations can neither fold nor reassociate across nodes: composed code then
rounds exactly as the plan interpreter, which keeps each node's result apart, does. */
inline void settle(Signal& s) {
	for (int c = 0; c < s.channels; c++) {
#if defined(__x86_64__) || defined(__i386__)
		asm volatile("" : "+x"(s.v[c]));
#elif defined(__aarch64__)
		asm volatile("" : "+w"(s.v[c]));
#else
		asm volatile("" : "+m"(s.v[c]));
#endif
	}
}

/** settle() for one value: the compiled tier's single-channel path. */
inline void settle(float& x) {
#if defined(__x86_64__) || defined(__i386__)
	asm volatile("" : "+x"(x));
#elif defined(__aarch64__)
	asm volatile("" : "+w"(x));
#else
	asm volatile("" : "+m"(x));
#endif
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
	/** One channel: what process() computes for channel c. */
	float step(int c, float freq, float sampleTime) {
		phase[c] += freq * sampleTime;
		if (phase[c] >= 1.f)
			phase[c] -= 1.f;
		return phase[c];
	}
	void process(Signal& out, const Signal& freq, float sampleTime) {
		int n = std::max(1, freq.channels);
		out.channels = n;
		for (int c = 0; c < n; c++)
			out.v[c] = step(c, lane(freq, c), sampleTime);
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
	float step(int c, float x) { return trigger[c].process(x, low, high) ? 1.f : 0.f; }
	void process(Signal& out, const Signal& x) {
		int n = std::max(1, x.channels);
		out.channels = n;
		for (int c = 0; c < n; c++)
			out.v[c] = step(c, lane(x, c));
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
	float step(int c, float trigger, float sampleTime) {
		if (trigger > 0.f)
			pulse[c].trigger(duration);
		return pulse[c].process(sampleTime) ? 1.f : 0.f;
	}
	void process(Signal& out, const Signal& trigger, float sampleTime) {
		int n = std::max(1, trigger.channels);
		out.channels = n;
		for (int c = 0; c < n; c++)
			out.v[c] = step(c, lane(trigger, c), sampleTime);
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

// --- Native extension node types (from kernel libraries) ------------------------------------

/** Bumped whenever Node, NodeType or NodeRegistrar change incompatibly. */
constexpr int GRAPH_ABI = 1;

/** The most signals a node reads. */
constexpr int MAX_NODE_INPUTS = 8;

/** One instance of a native node type, owned by one plan. */
struct Node {
	virtual ~Node() = default;
	/** One frame, on Rack's engine thread, under Kernel::process's rules. Read the `NodeType::inputs` signals
	in `inputs` and write `out`, its channel count (0 to MAX_CHANNELS) included. A count outside that
	range fails the plan: RackPy silences the module and records a diagnostic. */
	virtual void process(Signal& out, const Signal* const* inputs, const rack::engine::Module::ProcessArgs& args) noexcept = 0;
	/** Rack's reset (the module's Initialize): start the node's state over. */
	virtual void reset() noexcept {}
};

/** A kind of node that Python graphs use as Graph.node("namespace.Name", *inputs, **args). */
struct NodeType {
	/** Short ("Fold"); Python sees it as "namespace.Fold", the library's namespace. */
	std::string name;
	std::string doc;
	/** How many signals it reads, 0 to MAX_NODE_INPUTS. */
	int inputs = 1;
	/** The names of the numbers that configure it, in the order `create` receives them. */
	std::vector<std::string> args;
	/** Main thread. Throw std::invalid_argument for numbers the type cannot use. */
	std::function<std::unique_ptr<Node>(const std::vector<double>& args)> create;
};

/** What a kernel library's graph-node registration adds its types to. */
struct NodeRegistrar {
	virtual void add(NodeType type) = 0;

protected:
	~NodeRegistrar() = default;
};

}  // namespace graph
}  // namespace rackpy

/** Defines a kernel library's graph-node entry points (RP-21C), next to its
RACKPY_KERNEL_LIBRARY, whose namespace the node types share:

	RACKPY_GRAPH_NODES(registrar) {
		registrar.add(rackpy::graph::NodeType{"Fold", "...", 1, {"drive"}, create});
	}
*/
#define RACKPY_GRAPH_NODES(REGISTRAR) \
	extern "C" __attribute__((visibility("default"))) int rackpy_graph_abi() { return rackpy::graph::GRAPH_ABI; } \
	extern "C" __attribute__((visibility("default"))) void rackpy_register_graph_nodes(rackpy::graph::NodeRegistrar& REGISTRAR)
