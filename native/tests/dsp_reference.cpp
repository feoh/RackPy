// SPDX-License-Identifier: GPL-3.0-or-later
// Differential reference for the DSP bindings (RP-18): evaluates Rack's own
// rack::dsp code over edge cases and seeded random inputs, compiled with the
// SDK's flags, and prints every case as JSON. tests/dsp replays each case
// through rackpy.raw.dsp inside Rack and compares the results bit for bit.
//
// Encoding as in simd_reference.cpp: floats as 32-bit patterns ("0x3f800000"),
// float_4 as four patterns, ints as numbers. Inputs pass through opaque() so the
// compiler cannot fold a case at compile time. Rack's functions are called through the same
// out-of-line wrappers as the bindings (native/bindings/dsp/calls.hpp explains why).
#include <rack.hpp>

#include "../bindings/dsp/calls.hpp"

#include <array>
#include <cstdlib>
#include <memory>
#include <cinttypes>
#include <complex>
#include <type_traits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace rack;
using simd::float_4;
namespace calls = rackpy::dsp_calls;
using calls::Poly;
using calls::polyN;
using calls::Method;

namespace {

template <typename T>
T opaque(T x) {
	volatile T v = x;
	return v;
}

float_4 opaque(float_4 x) {
	float lanes[4];
	x.store(lanes);
	volatile float* p = lanes;
	float out[4] = {p[0], p[1], p[2], p[3]};
	return float_4::load(out);
}

std::string bitsOf(float x) {
	uint32_t bits;
	std::memcpy(&bits, &x, sizeof bits);
	char buf[16];
	std::snprintf(buf, sizeof buf, "0x%08" PRIx32, bits);
	return buf;
}

std::string f(float x) {
	return "{\"f\": \"" + bitsOf(x) + "\"}";
}

std::string f4(float_4 x) {
	return "{\"f4\": [\"" + bitsOf(x.s[0]) + "\", \"" + bitsOf(x.s[1]) + "\", \"" + bitsOf(x.s[2]) + "\", \"" + bitsOf(x.s[3]) +
		"\"]}";
}

std::string i(long long x) {
	return std::to_string(x);
}

std::string list(const std::vector<std::string>& items) {
	std::string out = "[";
	for (size_t k = 0; k < items.size(); k++)
		out += (k ? ", " : "") + items[k];
	return out + "]";
}

std::string floats(const float* x, size_t n) {
	std::vector<std::string> items;
	for (size_t k = 0; k < n; k++)
		items.push_back(f(x[k]));
	return list(items);
}

/** A name, such as an enumerator's: plain identifiers, so nothing needs escaping. */
std::string str(const std::string& x) {
	return "\"" + x + "\"";
}

/** An enum's enumerators by name, in declaration order: the replay checks each Python name and value. */
using Enumerators = std::vector<std::pair<std::string, int>>;

int valueOf(const Enumerators& table, const std::string& name) {
	for (const auto& e : table)
		if (e.first == name)
			return e.second;
	std::fprintf(stderr, "no enumerator %s\n", name.c_str());
	std::abort();
}

std::string enumerators(const Enumerators& table) {
	std::vector<std::string> out;
	for (const auto& e : table)
		out.push_back(list({str(e.first), i(e.second)}));
	return list(out);
}

std::vector<std::string> cases;

void add(const std::string& fn, const std::vector<std::string>& args, const std::string& result) {
	cases.push_back("{\"fn\": \"" + fn + "\", \"args\": " + list(args) + ", \"result\": " + result + "}");
}

const float INF = std::numeric_limits<float>::infinity();
const float NAN_ = std::numeric_limits<float>::quiet_NaN();
const std::vector<float> EDGES = {0.f, -0.f, 1.f, -1.f, 0.5f, -0.5f, 0.25f, 0.75f, 1.5f, -1.5f, 2.5f, -2.5f, 1.f / 3.f, 0.1f,
	1e-7f, -1e-7f, 1e-45f, 1.1754944e-38f, 3.4e38f, -3.4e38f, 10.f, -10.f, 100.f, -100.f, 127.5f, -127.5f, 128.9f, 0.999f,
	-0.999f, 2147483648.f, INF, -INF, NAN_};

uint32_t state = 0x2545f491u;
uint32_t next() {
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}
float uniform(float lo, float hi) {
	return lo + (hi - lo) * (float) (next() >> 8) / 16777216.f;
}

std::vector<float> SCALARS;
std::vector<float_4> VECTORS;

void makeInputs() {
	SCALARS = EDGES;
	for (int k = 0; k < 40; k++)
		SCALARS.push_back(uniform(-20.f, 20.f));
	for (int k = 0; k < 16; k++)
		SCALARS.push_back(uniform(0.f, 1.f));
	std::vector<float> lanes = SCALARS;
	while (lanes.size() % 4)
		lanes.push_back(uniform(-1.f, 1.f));
	for (size_t k = 0; k < lanes.size(); k += 4)
		VECTORS.push_back(float_4(lanes[k], lanes[k + 1], lanes[k + 2], lanes[k + 3]));
	for (size_t k = 1; k + 4 <= EDGES.size(); k += 4)
		VECTORS.push_back(float_4(EDGES[k], EDGES[k + 1], EDGES[k + 2], EDGES[k + 3]));
}

/** float and float_4 instantiations of a one-argument template, over every input. */
template <typename F, typename G>
void unary(const char* name, F scalar, G vector) {
	for (float x : SCALARS)
		add(std::string(name), {f(x)}, f(scalar(opaque(x))));
	for (float_4 x : VECTORS)
		add(std::string(name) + "_4", {f4(x)}, f4(vector(opaque(x))));
}

void common() {
	add("FREQ_C4", {}, f(dsp::FREQ_C4));
	add("FREQ_A4", {}, f(dsp::FREQ_A4));
	add("FREQ_SEMITONE", {}, f(dsp::FREQ_SEMITONE));
	unary("sinc", [](float x) { return calls::sinc(x); }, [](float_4 x) { return calls::sinc(x); });
	unary("amplitude_to_db", [](float x) { return calls::amplitudeToDb<float>(x); }, [](float_4 x) { return calls::amplitudeToDb<float_4>(x); });
	unary("db_to_amplitude", [](float x) { return calls::dbToAmplitude<float>(x); }, [](float_4 x) { return calls::dbToAmplitude<float_4>(x); });
	unary("quadratic_bipolar", [](float x) { return calls::quadraticBipolar<float>(x); },
		[](float_4 x) { return calls::quadraticBipolar<float_4>(x); });
	unary("cubic", [](float x) { return calls::cubic<float>(x); }, [](float_4 x) { return calls::cubic<float_4>(x); });
	unary("quartic_bipolar", [](float x) { return calls::quarticBipolar<float>(x); },
		[](float_4 x) { return calls::quarticBipolar<float_4>(x); });
	unary("quintic", [](float x) { return calls::quintic<float>(x); }, [](float_4 x) { return calls::quintic<float_4>(x); });
	unary("sqrt_bipolar", [](float x) { return calls::sqrtBipolar<float>(x); }, [](float_4 x) { return calls::sqrtBipolar<float_4>(x); });
	for (float b : {2.f, 10.f, 0.5f, 1.f, -2.f, 0.f, INF})
		for (float x : SCALARS) {
			add("exponential_bipolar", {f(b), f(x)}, f(calls::exponentialBipolar<float>(opaque(b), opaque(x))));
		}
	for (float b : {2.f, 10.f, 0.5f})
		for (float_4 x : VECTORS)
			add("exponential_bipolar_4", {f4(float_4(b)), f4(x)}, f4(calls::exponentialBipolar<float_4>(opaque(float_4(b)), opaque(x))));
}

template <size_t N>
void polyCases() {
	float a[N];
	float_4 a4[N];
	for (size_t k = 0; k < N; k++) {
		a[k] = uniform(-2.f, 2.f);
		a4[k] = float_4(uniform(-2.f, 2.f), uniform(-2.f, 2.f), a[k], uniform(-2.f, 2.f));
	}
	std::vector<std::string> coefficients, vectorCoefficients;
	for (size_t k = 0; k < N; k++) {
		coefficients.push_back(f(a[k]));
		vectorCoefficients.push_back(f4(a4[k]));
	}
	for (float x : {0.f, 1.f, -1.f, 0.5f, -0.75f, 1.25f, 3.f, NAN_, INF}) {
		float xo = opaque(x);
		// The same out-of-line instantiations the bindings call (poly.hpp explains why).
		add("poly_direct", {list(coefficients), f(x)}, f(polyN<Poly::Direct, float, N>(a, xo)));
		add("poly_horner", {list(coefficients), f(x)}, f(polyN<Poly::Horner, float, N>(a, xo)));
		add("poly_estrin", {list(coefficients), f(x)}, f(polyN<Poly::Estrin, float, N>(a, xo)));
	}
	for (float_4 x : {float_4(0.f, 1.f, -1.f, 0.5f), float_4(-0.75f, 1.25f, 3.f, NAN_)}) {
		float_4 xo = opaque(x);
		add("poly_direct_4", {list(vectorCoefficients), f4(x)}, f4(polyN<Poly::Direct, float_4, N>(a4, xo)));
		add("poly_horner_4", {list(vectorCoefficients), f4(x)}, f4(polyN<Poly::Horner, float_4, N>(a4, xo)));
		add("poly_estrin_4", {list(vectorCoefficients), f4(x)}, f4(polyN<Poly::Estrin, float_4, N>(a4, xo)));
	}
}

template <size_t... N>
void allPolys(std::index_sequence<N...>) {
	(polyCases<N + 1>(), ...);
}

void approx() {
	allPolys(std::make_index_sequence<32>());
	// exp2Floor<float> is defined only where x + 127 is in (-1, 256): the bindings refuse the rest.
	for (float x : SCALARS) {
		float y = x + 127;
		if (!(y > -1.f && y < 256.f))
			continue;
		float xf = 0.f;
		float r = calls::exp2Floor<float>(opaque(x), &xf);
		add("exp2_floor", {f(x)}, list({f(r), f(xf)}));
		add("exp2_taylor5", {f(x)}, f(calls::exp2Taylor5<float>(opaque(x))));
	}
	for (float_4 x : VECTORS) {
		float_4 xf = 0.f;
		float_4 r = calls::exp2Floor<float_4>(opaque(x), &xf);
		add("exp2_floor_4", {f4(x)}, list({f4(r), f4(xf)}));
		add("exp2_taylor5_4", {f4(x)}, f4(calls::exp2Taylor5<float_4>(opaque(x))));
	}
}

template <typename To, typename From>
std::string out(To v) {
	if constexpr (std::is_same<To, float>::value)
		return f(v);
	else if constexpr (std::is_same<To, dsp::Int24>::value)
		return i((int32_t) v.i);
	else
		return i((long long) v);
}

template <typename To>
void fromFloat(const char* name) {
	for (float x : SCALARS)
		add(std::string("convert.") + name + ".float", {f(x)}, out<To, float>(calls::convert<To, float>(opaque(x))));
}

template <typename From>
void toFloat(const char* name, const std::vector<long long>& values) {
	for (long long v : values)
		add(std::string("convert.float.") + name, {i(v)}, f(calls::convert<float, From>(opaque((From) v))));
}

void conversions() {
	add("convert.float.float", {f(0.25f)}, f(calls::convert<float, float>(opaque(0.25f))));
	toFloat<int8_t>("int8", {0, 1, -1, 64, 127, -128});
	toFloat<int16_t>("int16", {0, 1, -1, 16384, 32767, -32768});
	toFloat<int32_t>("int32", {0, 1, -1, 1073741824, INT32_MAX, INT32_MIN});
	toFloat<int64_t>("int64", {0, 1, -1, 4611686018427387904LL, INT64_MAX, INT64_MIN});
	for (long long v : {0LL, 1LL, -1LL, 4194304LL, 8388607LL, -8388608LL})
		add("convert.float.int24", {i(v)}, f(calls::convert<float, dsp::Int24>(dsp::Int24(opaque((int32_t) v)))));
	fromFloat<int8_t>("int8");
	fromFloat<int16_t>("int16");
	fromFloat<dsp::Int24>("int24");
	fromFloat<int32_t>("int32");
	fromFloat<int64_t>("int64");
	// Int24 keeps the low 24 bits of an int32_t (GCC's bit-field).
	for (int32_t v : {0, 1, -1, 8388607, -8388608, 8388608, -8388609, 0x12345678, INT32_MAX, INT32_MIN})
		add("int24", {i(v)}, i((int32_t) dsp::Int24(opaque(v)).i));
	// Buffer conversions: in, out and len.
	float in[6] = {0.f, 0.5f, -0.5f, 1.f, -1.f, 2.f};
	int16_t o16[6];
	calls::convertBuffer<int16_t, float>(in, o16, 6);
	add("convert_buffer.int16.float", {floats(in, 6)}, list({i(o16[0]), i(o16[1]), i(o16[2]), i(o16[3]), i(o16[4]), i(o16[5])}));
	int16_t i16[4] = {0, 16384, -32768, 32767};
	float of[4];
	calls::convertBuffer<float, int16_t>(i16, of, 4);
	add("convert_buffer.float.int16", {list({i(i16[0]), i(i16[1]), i(i16[2]), i(i16[3])})}, floats(of, 4));
	dsp::Int24 o24[3];
	calls::convertBuffer<dsp::Int24, float>(in, o24, 3);
	add("convert_buffer.int24.float", {floats(in, 3)}, list({i(o24[0].i), i(o24[1].i), i(o24[2].i)}));
}

void windows() {
	unary("hann", [](float p) { return calls::hann<float>(p); }, [](float_4 p) { return calls::hann<float_4>(p); });
	unary("blackman_nuttall", [](float p) { return calls::blackmanNuttall<float>(p); },
		[](float_4 p) { return calls::blackmanNuttall<float_4>(p); });
	unary("blackman_harris", [](float p) { return calls::blackmanHarris<float>(p); },
		[](float_4 p) { return calls::blackmanHarris<float_4>(p); });
	for (float alpha : {0.16f, 0.f, 0.5f})
		for (float p : SCALARS)
			add("blackman", {f(alpha), f(p)}, f(calls::blackman<float>(opaque(alpha), opaque(p))));
	for (float_4 p : VECTORS)
		add("blackman_4", {f4(float_4(0.16f)), f4(p)}, f4(calls::blackman<float_4>(opaque(float_4(0.16f)), opaque(p))));
	for (int len : {0, 1, 2, 5, 16, 64}) {
		std::vector<float> x((size_t) std::max(len, 1));
		for (float& v : x)
			v = uniform(-1.f, 1.f);
		std::string args = floats(x.data(), x.size());
		auto run = [&](const char* name, auto fn) {
			std::vector<float> y = x;
			fn(y.data());
			add(name, {args, i(len)}, floats(y.data(), y.size()));
		};
		run("hann_window", [&](float* y) { calls::hannWindow(y, opaque(len)); });
		run("blackman_nuttall_window", [&](float* y) { calls::blackmanNuttallWindow(y, opaque(len)); });
		run("blackman_harris_window", [&](float* y) { calls::blackmanHarrisWindow(y, opaque(len)); });
		std::vector<float> y = x;
		calls::blackmanWindow(opaque(0.16f), y.data(), opaque(len));
		add("blackman_window", {f(0.16f), args, i(len)}, floats(y.data(), y.size()));
	}
}

// The systems the ODE cases solve; tests/dsp's replay defines the same ones in Python, with exact
// float32 arithmetic (copies, negation, multiplication by 0.5 and addition of t). They go through
// the bindings' stepper instantiations (calls.hpp), with a context pointer they do not use.
void oscillator(void*, float, const float* x, float* dxdt) {
	dxdt[0] = x[1];
	dxdt[1] = -x[0];
}
void decay(void*, float t, const float* x, float* dxdt) {
	for (int k = 0; k < 3; k++)
		dxdt[k] = -0.5f * x[k] + t;
}
void oscillator4(void*, float_4, const float_4* x, float_4* dxdt) {
	dxdt[0] = x[1];
	dxdt[1] = -x[0];
}

template <typename T>
void stepWith(const std::string& method, T t, T dt, T* x, int len, calls::OdeSystem<T> fn) {
	if (method == "euler")
		calls::step<Method::Euler, T>(t, dt, x, len, fn, nullptr);
	else if (method == "rk2")
		calls::step<Method::RK2, T>(t, dt, x, len, fn, nullptr);
	else
		calls::step<Method::RK4, T>(t, dt, x, len, fn, nullptr);
}

void ode() {
	for (const char* system : {"oscillator", "decay"}) {
		bool osc = std::string(system) == "oscillator";
		int len = osc ? 2 : 3;
		for (const char* method : {"euler", "rk2", "rk4"}) {
			float x[3] = {1.f, 0.f, 0.25f};
			float t = 0.f, dt = 0.01f;
			std::string start = floats(x, (size_t) len);
			for (int k = 0; k < 100; k++, t += dt)
				stepWith<float>(method, t, dt, x, len, osc ? oscillator : decay);
			add(std::string("ode.") + method + "." + system, {start, f(dt), i(100)}, floats(x, (size_t) len));
		}
	}
	for (const char* method : {"euler", "rk2", "rk4"}) {
		float_4 x[2] = {float_4(1.f, 0.5f, -1.f, 2.f), float_4(0.f)};
		float_4 t = 0.f, dt = 0.01f;
		std::vector<std::string> start = {f4(x[0]), f4(x[1])};
		for (int k = 0; k < 100; k++, t += dt)
			stepWith<float_4>(method, t, dt, x, 2, oscillator4);
		add(std::string("ode4.") + method + ".oscillator", {list(start), f(0.01f), i(100)}, list({f4(x[0]), f4(x[1])}));
	}
}


// --- digital.hpp: every object starts fresh and runs a whole stream ----------------------

std::string b(bool x) {
	return x ? "true" : "false";
}

void digital() {
	std::vector<bool> bools;
	for (int k = 0; k < 64; k++)
		bools.push_back(next() & 1);
	std::vector<std::string> args, processed, events;
	dsp::BooleanTrigger bt, bte;
	for (bool v : bools) {
		args.push_back(b(v));
		bool r = bt.process(opaque(v));
		processed.push_back(list({b(r), i(bt.s)}));
		int e = bte.processEvent(opaque(v));
		events.push_back(list({i(e), i(bte.s)}));
	}
	add("boolean_trigger.process", {list(args)}, list(processed));
	add("boolean_trigger.process_event", {list(args)}, list(events));

	std::vector<float> inputs = {0.f, 0.5f, 1.f, 1.f, 0.f, -1.f, 2.f, NAN_, 0.5f, 1.5f, -0.f, 1.f, 0.1f, 2.f, INF, -INF};
	for (int k = 0; k < 48; k++)
		inputs.push_back(uniform(-0.5f, 2.5f));
	for (auto thresholds : std::vector<std::pair<float, float>>{{0.f, 1.f}, {0.1f, 2.f}, {1.f, 0.f}}) {
		std::vector<std::string> in, out, ev;
		dsp::SchmittTrigger st, ste;
		for (float x : inputs) {
			in.push_back(f(x));
			bool r = st.process(opaque(x), thresholds.first, thresholds.second);
			out.push_back(list({b(r), i(st.s)}));
			int e = ste.processEvent(opaque(x), thresholds.first, thresholds.second);
			ev.push_back(list({i(e), i(ste.s)}));
		}
		add("schmitt.process", {list(in), f(thresholds.first), f(thresholds.second)}, list(out));
		add("schmitt.process_event", {list(in), f(thresholds.first), f(thresholds.second)}, list(ev));
	}
	{
		std::vector<std::string> in, out;
		dsp::TSchmittTrigger<float_4> st;
		for (size_t k = 0; k + 4 <= inputs.size(); k += 2) {
			float_4 x(inputs[k], inputs[k + 1], inputs[k + 2], inputs[k + 3]);
			in.push_back(f4(x));
			float_4 r = st.process(opaque(x), 0.1f, 1.5f);
			out.push_back(list({f4(r), f4(st.state), f4(st.isHigh())}));
		}
		add("schmitt4.process", {list(in), f(0.1f), f(1.5f)}, list(out));
	}

	// Pulse generator: a script of triggers (positive) and steps (negative: -dt).
	std::vector<float> script = {1e-3f, -2e-4f, -2e-4f, 5e-4f, -4e-4f, -4e-4f, -4e-4f, 2e-3f, -1e-3f, 1e-4f, -1.5e-3f, -1e-3f, 0.f,
		-1e-4f};
	std::vector<std::string> ops, results;
	dsp::PulseGenerator pg;
	for (float op : script) {
		ops.push_back(f(op));
		if (op >= 0.f) {
			pg.trigger(opaque(op));
			results.push_back(list({b(pg.isHigh()), f(pg.remaining)}));
		}
		else {
			bool r = pg.process(opaque(-op));
			results.push_back(list({b(r), f(pg.remaining)}));
		}
	}
	add("pulse.script", {list(ops)}, list(results));
	dsp::PulseGenerator pd;
	pd.trigger();
	add("pulse.default_trigger", {}, f(pd.remaining));

	std::vector<std::string> deltas, times;
	dsp::Timer timer;
	dsp::TTimer<float_4> timer4;
	std::vector<std::string> deltas4, times4;
	for (int k = 0; k < 32; k++) {
		float dt = k == 16 ? -0.5f : uniform(0.f, 0.1f);
		deltas.push_back(f(dt));
		times.push_back(list({f(timer.process(opaque(dt))), f(timer.getTime())}));
		float_4 d4(dt, 2 * dt, -dt, 1e-3f);
		deltas4.push_back(f4(d4));
		times4.push_back(list({f4(timer4.process(opaque(d4))), f4(timer4.getTime())}));
	}
	add("timer.process", {list(deltas)}, list(times));
	add("timer4.process", {list(deltas4)}, list(times4));

	for (uint32_t division : {1u, 3u, 0u, 5u, 4294967295u}) {
		dsp::ClockDivider cd;
		cd.setDivision(opaque(division));
		std::vector<std::string> ticks;
		for (int k = 0; k < 12; k++) {
			bool r = cd.process();
			ticks.push_back(list({b(r), i(cd.getClock())}));
		}
		add("clock_divider.process", {i(division)}, list(ticks));
	}
}

// --- filter.hpp: fresh objects over whole streams, through the bindings' member wrappers ----

#define RACKPY_DEPRECATED_CALL(...) \
	_Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"") __VA_ARGS__ \
		_Pragma("GCC diagnostic pop")

using calls::member;

template <typename T>
std::string enc(T x) {
	if constexpr (std::is_same<T, float>::value)
		return f(x);
	else
		return f4(x);
}

template <typename T>
T lane(float x, int k) {
	if constexpr (std::is_same<T, float>::value)
		return x;
	else
		return float_4(x, x * 0.5f + 0.25f * k, -x, 1.f - x);
}

std::vector<float> STREAM;

/** The stream as T, with lane pattern k for float_4, as the arguments the replay passes. */
template <typename T>
std::vector<std::string> inputs(int k) {
	std::vector<std::string> in;
	for (float x : STREAM)
		in.push_back(enc(lane<T>(x, k)));
	return in;
}

template <typename T>
void singlePole(const char* suffix) {
	std::vector<std::string> in = inputs<T>(0);
	for (float fc : {0.01f, 0.1f, 0.45f, 0.f}) {
		dsp::TRCFilter<T> rc;
		member<&dsp::TRCFilter<T>::setCutoffFreq>(rc, T(fc));
		std::vector<std::string> out;
		for (float x : STREAM) {
			member<&dsp::TRCFilter<T>::process>(rc, lane<T>(x, 0));
			out.push_back(list({enc(member<&dsp::TRCFilter<T>::lowpass>(rc)), enc(member<&dsp::TRCFilter<T>::highpass>(rc)), enc(rc.c)}));
		}
		add(std::string("rc") + suffix, {list(in), f(fc)}, list(out));
	}
	for (float lambda : {30.f, 1000.f, 0.f}) {
		using E = dsp::TExponentialFilter<T>;
		using P = dsp::TPeakFilter<T>;
		using S = dsp::TSlewLimiter<T>;
		using X = dsp::TExponentialSlewLimiter<T>;
		E e;
		P p;
		S sl;
		X xs;
		float tau = 1.f / std::max(lambda, 1.f), rise = lambda, fall = lambda * 0.5f, riseTau = 0.01f;
		float fallTau = lambda > 0.f ? 1.f / lambda : 0.f, step = 1.f / 48000.f;
		member<&E::setLambda>(e, T(lambda));
		member<&P::setTau>(p, T(tau));
		member<&S::setRiseFall>(sl, T(rise), T(fall));
		member<&X::setRiseFallTau>(xs, T(riseTau), T(fallTau));
		std::vector<std::string> out;
		for (float x : STREAM) {
			T v = lane<T>(x, 1);
			T dt = T(step);
			T a = member<static_cast<T (E::*)(T, T)>(&E::process)>(e, dt, v);
			T b2 = member<static_cast<T (P::*)(T, T)>(&P::process)>(p, dt, v);
			T c2 = member<static_cast<T (S::*)(T, T)>(&S::process)>(sl, dt, v);
			T d2 = member<static_cast<T (X::*)(T, T)>(&X::process)>(xs, dt, v);
			out.push_back(list({enc(a), enc(b2), enc(c2), enc(d2)}));
		}
		add(std::string("smoothers") + suffix, {list(inputs<T>(1)), f(lambda), f(tau), f(rise), f(fall), f(riseTau), f(fallTau), f(step)},
			list({list(out), enc(xs.riseLambda), enc(xs.fallLambda)}));
	}
	// The deprecated forms: process(in) is process(1, in); PeakFilter's peak() and setRate().
	{
		using E = dsp::TExponentialFilter<T>;
		using P = dsp::TPeakFilter<T>;
		using S = dsp::TSlewLimiter<T>;
		using X = dsp::TExponentialSlewLimiter<T>;
		E e;
		P p;
		S sl;
		X xs;
		member<&E::setLambda>(e, T(0.25f));
		RACKPY_DEPRECATED_CALL(member<&P::setRate>(p, T(0.75f));)
		member<&S::setRiseFall>(sl, T(0.5f), T(0.25f));
		member<&X::setRiseFall>(xs, T(0.5f), T(0.125f));
		std::vector<std::string> out;
		for (float x : STREAM) {
			T v = lane<T>(x, 2);
			T a, b2, c2, d2, pk;
			RACKPY_DEPRECATED_CALL(a = member<static_cast<T (E::*)(T)>(&E::process)>(e, v);)
			RACKPY_DEPRECATED_CALL(b2 = member<static_cast<T (P::*)(T)>(&P::process)>(p, v);)
			RACKPY_DEPRECATED_CALL(c2 = member<static_cast<T (S::*)(T)>(&S::process)>(sl, v);)
			RACKPY_DEPRECATED_CALL(d2 = member<static_cast<T (X::*)(T)>(&X::process)>(xs, v);)
			RACKPY_DEPRECATED_CALL(pk = member<&P::peak>(p);)
			out.push_back(list({enc(a), enc(b2), enc(c2), enc(d2), enc(pk)}));
		}
		add(std::string("deprecated_smoothers") + suffix, {list(inputs<T>(2))}, list(out));
	}
}

template <int B, int A, typename T>
void iir(const char* suffix) {
	using F = dsp::IIRFilter<B, A, T>;
	F filter;
	T b[B], a[A - 1];
	std::vector<std::string> bs, as;
	for (int k = 0; k < B; k++) {
		b[k] = lane<T>(uniform(-1.f, 1.f), k);
		bs.push_back(enc(b[k]));
	}
	for (int k = 0; k < A - 1; k++) {
		a[k] = lane<T>(uniform(-0.4f, 0.4f) / (A - 1), k);
		as.push_back(enc(a[k]));
	}
	const T* bp = b;
	const T* ap = a;
	member<&F::setCoefficients>(filter, bp, ap);
	std::vector<std::string> in, out;
	for (size_t k = 0; k < 48; k++) {
		T x = k == 0 ? T(1.f) : lane<T>(STREAM[k % STREAM.size()], 3);
		in.push_back(enc(x));
		out.push_back(enc(member<&F::process>(filter, x)));
	}
	std::vector<std::string> response, frequencies;
	for (float fr : {0.f, 0.01f, 0.125f, 0.25f, 0.49f}) {
		float angular = opaque(2.f * (float) M_PI) * fr;
		frequencies.push_back(list({f(fr), f(angular)}));
		std::complex<T> h = member<&F::getTransferFunction>(filter, T(angular));
		response.push_back(list({enc(h.real()), enc(h.imag()), enc(member<&F::getFrequencyResponse>(filter, T(fr))),
			enc(member<&F::getFrequencyPhase>(filter, T(fr)))}));
	}
	std::string name = "iir" + std::string(suffix) + "." + std::to_string(B) + "." + std::to_string(A);
	add(name, {list(bs), list(as), list(in), list(frequencies)}, list({list(out), list(response)}));
}

template <typename T>
Enumerators biquadTypes() {
	using F = dsp::TBiquadFilter<T>;
	return {{"LOWPASS_1POLE", F::LOWPASS_1POLE}, {"HIGHPASS_1POLE", F::HIGHPASS_1POLE}, {"LOWPASS", F::LOWPASS},
		{"HIGHPASS", F::HIGHPASS}, {"LOWSHELF", F::LOWSHELF}, {"HIGHSHELF", F::HIGHSHELF}, {"BANDPASS", F::BANDPASS},
		{"PEAK", F::PEAK}, {"NOTCH", F::NOTCH}, {"NUM_TYPES", F::NUM_TYPES}};
}

template <typename T>
void biquads(const char* suffix) {
	using F = dsp::TBiquadFilter<T>;
	using Base = dsp::IIRFilter<3, 3, T>;  // the bindings call the inherited methods on the base, as here
	{
		F fresh;
		Base& base = fresh;
		add(std::string("biquad_default") + suffix, {}, list({enc(fresh.b[0]), enc(fresh.a[0]), enc(member<&Base::process>(base, T(1.f)))}));
	}
	add(std::string("biquad_types") + suffix, {}, enumerators(biquadTypes<T>()));
	// Each type by name, so the replay reaches each Python enumerator by its name.
	for (const auto& type : biquadTypes<T>())
		for (auto p : std::vector<std::array<float, 3>>{{0.1f, 0.707f, 1.f}, {0.01f, 2.f, 4.f}, {0.3f, 0.5f, 0.25f}, {0.49f, 10.f, 1.f}}) {
			F filter;
			member<&F::setParameters>(filter, (typename F::Type) type.second, p[0], p[1], p[2]);
			std::vector<std::string> coefficients = {enc(filter.b[0]), enc(filter.b[1]), enc(filter.b[2]), enc(filter.a[0]), enc(filter.a[1])};
			Base& base = filter;
			std::vector<std::string> impulse;
			for (int k = 0; k < 16; k++)
				impulse.push_back(enc(member<&Base::process>(base, T(k == 0 ? 1.f : 0.f))));
			std::vector<std::string> magnitudes, frequencies;
			for (float fr : {0.01f, 0.1f, 0.25f}) {
				frequencies.push_back(f(fr));
				magnitudes.push_back(enc(member<&Base::getFrequencyResponse>(base, T(fr))));
			}
			add(std::string("biquad") + suffix, {str(type.first), f(p[0]), f(p[1]), f(p[2]), list(frequencies)},
				list({list(coefficients), list(impulse), list(magnitudes)}));
		}
}

void filters() {
	STREAM = {0.f, 1.f, 1.f, 1.f, 0.f, -1.f, 0.5f, 10.f, -10.f, 0.f, 0.f, NAN_, 1.f, INF, 0.f, 0.f};
	for (int k = 0; k < 48; k++)
		STREAM.push_back(uniform(-5.f, 5.f));
	singlePole<float>("");
	singlePole<float_4>("4");
	iir<2, 2, float>("");
	iir<3, 3, float>("");
	iir<4, 2, float>("");
	iir<2, 5, float>("");
	iir<8, 8, float>("");
	iir<3, 3, float_4>("4");
	iir<5, 4, float_4>("4");
	biquads<float>("");
	biquads<float_4>("4");
}

// --- vumeter.hpp and midi.hpp: op scripts, recorded with the state after each op ----------

void vumeters() {
	RACKPY_DEPRECATED_CALL(dsp::VuMeter old;)
	old.dBScaled = 0.f;
	std::vector<std::string> values, states;
	for (float v : {1.f, 0.5f, 2.f, 0.f, -0.25f, 1e-3f, 0.708f, INF, NAN_}) {
		values.push_back(f(v));
		member<&dsp::VuMeter::setValue>(old, opaque(v));
		std::vector<std::string> lights;
		for (int k = 0; k < 7; k++)
			lights.push_back(f(member<&dsp::VuMeter::getBrightness>(old, k)));
		states.push_back(list({f(old.dBScaled), list(lights)}));
	}
	add("vumeter.old", {list(values)}, list(states));
	for (int mode : {0, 1}) {
		dsp::VuMeter2 meter;
		meter.mode = (dsp::VuMeter2::Mode) mode;
		std::vector<std::string> in, out;
		for (size_t k = 0; k < STREAM.size(); k++) {
			float x = STREAM[k] * 0.2f;
			in.push_back(f(x));
			member<&dsp::VuMeter2::process>(meter, opaque(1.f / 1000.f), x);
			out.push_back(list({f(meter.v), f(member<&dsp::VuMeter2::getBrightness>(meter, -12.f, -6.f)),
				f(member<&dsp::VuMeter2::getBrightness>(meter, -6.f, 0.f)), f(member<&dsp::VuMeter2::getBrightness>(meter, 0.f, 0.f))}));
			if (k == 40)
				member<&dsp::VuMeter2::reset>(meter);
		}
		add("vumeter2", {i(mode), f(1.f / 1000.f), list(in)}, list(out));
	}
}

template <int N>
struct Recorder : dsp::MidiGenerator<N> {
	std::vector<midi::Message> sent;
	void onMessage(const midi::Message& message) override { sent.push_back(message); }
};

std::string message(const midi::Message& m) {
	std::vector<std::string> bytes;
	for (uint8_t b2 : m.bytes)
		bytes.push_back(i(b2));
	return list({list(bytes), i(m.frame)});
}

template <int N>
void generator() {
	using G = dsp::MidiGenerator<N>;
	Recorder<N> rec;
	G& g = rec;
	std::vector<std::string> ops, out;
	auto record = [&](const std::string& op) {
		ops.push_back(op);
		std::vector<std::string> msgs;
		for (const auto& m : rec.sent)
			msgs.push_back(message(m));
		rec.sent.clear();
		// The fields the pressure setters compare against, after each op.
		std::vector<std::string> keyPressures;
		for (int c = 0; c < N; c++)
			keyPressures.push_back(i(g.keyPressures[c]));
		out.push_back(list({list(msgs), list(keyPressures), i(g.channelPressure)}));
	};
	int last = N - 1;
	member<&G::setFrame>(g, (int64_t) 100);
	record("[\"frame\", 100]");
	member<&G::setVelocity>(g, (int8_t) 90, 0);
	record("[\"vel\", 90, 0]");
	member<&G::setNoteGate>(g, (int8_t) 60, true, 0);
	record("[\"gate\", 60, true, 0]");
	member<&G::setNoteGate>(g, (int8_t) 64, true, 0);
	record("[\"gate\", 64, true, 0]");
	member<&G::setNoteGate>(g, (int8_t) 67, true, last);
	record("[\"gate\", 67, true, " + i(last) + "]");
	member<&G::setKeyPressure>(g, (int8_t) 33, last);
	record("[\"kp\", 33, " + i(last) + "]");
	member<&G::setChannelPressure>(g, (int8_t) 20);
	record("[\"cp\", 20]");
	member<&G::setChannelPressure>(g, (int8_t) 20);
	record("[\"cp\", 20]");
	member<&G::setCc>(g, (int8_t) 5, 74);
	record("[\"cc\", 5, 74]");
	member<&G::setModWheel>(g, (int8_t) 64);
	record("[\"mod\", 64]");
	member<&G::setVolume>(g, (int8_t) 100);
	record("[\"vol\", 100]");
	member<&G::setBalance>(g, (int8_t) 10);
	record("[\"bal\", 10]");
	member<&G::setPan>(g, (int8_t) -1);
	record("[\"pan\", -1]");
	member<&G::setSustainPedal>(g, (int8_t) 127);
	record("[\"sus\", 127]");
	member<&G::setPitchWheel>(g, (int16_t) 12345);
	record("[\"pw\", 12345]");
	member<&G::setPitchWheel>(g, (int16_t) -5);
	record("[\"pw\", -5]");
	for (const char* which : {"clk", "start", "cont", "stop"}) {
		for (bool v : {true, true, false}) {
			std::string w(which);
			if (w == "clk")
				member<&G::setClock>(g, v);
			else if (w == "start")
				member<&G::setStart>(g, v);
			else if (w == "cont")
				member<&G::setContinue>(g, v);
			else
				member<&G::setStop>(g, v);
			record("[\"" + w + "\", " + b(v) + "]");
		}
	}
	member<&G::setNoteGate>(g, (int8_t) 64, false, 0);
	record("[\"gate\", 64, false, 0]");
	member<&G::setChannels>(g, (uint8_t) (N > 1 ? 1 : 0));
	record("[\"channels\", " + i(N > 1 ? 1 : 0) + "]");
	member<&G::panic>(g);
	record("[\"panic\"]");
	member<&G::reset>(g);
	record("[\"reset\"]");
	add("generator." + std::to_string(N), {list(ops)}, list(out));
}

template <uint8_t N>
std::string parserState(dsp::MidiParser<N>& p) {
	using P = dsp::MidiParser<N>;
	std::vector<std::string> notes, gates, vels, afts, pws, mods, retrig, pitch, held, wheels, filters;
	for (int c = 0; c < N; c++) {
		filters.push_back(list({f(p.pwFilters[c].out), f(p.pwFilters[c].lambda), f(p.modFilters[c].out), f(p.modFilters[c].lambda)}));
		notes.push_back(i(p.notes[c]));
		gates.push_back(b(p.gates[c]));
		vels.push_back(i(p.velocities[c]));
		afts.push_back(i(p.aftertouches[c]));
		pws.push_back(i(p.pws[c]));
		mods.push_back(i(p.mods[c]));
		retrig.push_back(f(p.retriggerPulses[c].remaining));
		pitch.push_back(f(member<&P::getPitchVoltage>(p, (uint8_t) c)));
	}
	for (uint8_t note : p.heldNotes)
		held.push_back(i(note));
	uint8_t wc = member<&P::getWheelChannels>(p);
	for (uint8_t c = 0; c < wc; c++)
		wheels.push_back(list({f(member<&P::getPw>(p, c)), f(member<&P::getMod>(p, c))}));
	return list({list(notes), list(gates), list(vels), list(afts), list(pws), list(mods), list(held), i(p.rotateIndex), i(p.clock),
		b(p.pedal), i(member<&P::getChannels>(p)), list(pitch), list(wheels),
		list({f(p.clockPulse.remaining), f(p.clockDividerPulse.remaining), f(p.startPulse.remaining), f(p.stopPulse.remaining),
			f(p.continuePulse.remaining)}),
		list(retrig), list(filters),
		list({i(p.monoMode), i(p.polyMode), b(p.retriggerOnResume), b(p.releaseVelocityEnabled), f(p.pwRange), b(p.smooth),
			i(p.clockDivision)})});
}

// The modes by name: the scripts set them by name, so the replay reaches each Python enumerator by its name.
const Enumerators MONO_MODES = {{"LAST_PRIORITY_MODE", dsp::MidiParser<16>::LAST_PRIORITY_MODE},
	{"FIRST_PRIORITY_MODE", dsp::MidiParser<16>::FIRST_PRIORITY_MODE}, {"LOWEST_PRIORITY_MODE", dsp::MidiParser<16>::LOWEST_PRIORITY_MODE},
	{"HIGHEST_PRIORITY_MODE", dsp::MidiParser<16>::HIGHEST_PRIORITY_MODE}, {"NUM_MONO_MODES", dsp::MidiParser<16>::NUM_MONO_MODES}};
const Enumerators POLY_MODES = {{"ROTATE_MODE", dsp::MidiParser<16>::ROTATE_MODE}, {"REUSE_MODE", dsp::MidiParser<16>::REUSE_MODE},
	{"RESET_MODE", dsp::MidiParser<16>::RESET_MODE}, {"MPE_MODE", dsp::MidiParser<16>::MPE_MODE},
	{"NUM_POLY_MODES", dsp::MidiParser<16>::NUM_POLY_MODES}};

/** Rack's toJson, as JSON text inside the case's JSON. */
template <uint8_t N>
std::string parserJson(dsp::MidiParser<N>& p) {
	json_t* j = member<&dsp::MidiParser<N>::toJson>(p);
	char* text = json_dumps(j, JSON_SORT_KEYS | JSON_COMPACT);
	json_decref(j);
	std::string esc = "\"";
	for (const char* ch = text; *ch; ch++)
		esc += (*ch == '"' || *ch == '\\') ? std::string("\\") + *ch : std::string(1, *ch);
	std::free(text);
	return esc + "\"";
}

midi::Message messageOf(const std::vector<int>& bytes) {
	midi::Message m;
	// Byte by byte: GCC 16 misreads an assign() from ints into Message's 3-byte default as an overflow.
	m.bytes.clear();
	for (int x : bytes)
		m.bytes.push_back((uint8_t) x);
	m.frame = 7;
	return m;
}

std::string bytesOf(const std::vector<int>& bytes) {
	std::vector<std::string> bs;
	for (int x : bytes)
		bs.push_back(i(x));
	return list(bs);
}

template <uint8_t N>
void parser(const char* name, int channels, const char* poly, const char* mono) {
	using P = dsp::MidiParser<N>;
	P p;
	std::vector<std::string> ops, out;
	auto record = [&](const std::string& op) {
		ops.push_back(op);
		out.push_back(parserState(p));
	};
	member<&P::setChannels>(p, (uint8_t) channels);
	record("[\"channels\", " + i(channels) + "]");
	member<&P::setPolyMode>(p, (typename P::PolyMode) valueOf(POLY_MODES, poly));
	record("[\"poly\", " + str(poly) + "]");
	member<&P::setMonoMode>(p, (typename P::MonoMode) valueOf(MONO_MODES, mono));
	record("[\"mono\", " + str(mono) + "]");
	p.retriggerOnResume = true;
	p.releaseVelocityEnabled = true;
	p.clockDivision = 6;
	record("[\"flags\", true, true, 6]");
	std::vector<std::vector<int>> messages = {{0x90, 60, 100}, {0x91, 64, 90}, {0x92, 67, 80}, {0x90, 72, 70}, {0x93, 60, 0},
		{0x80, 64, 33}, {0xa0, 67, 50}, {0xd2, 77}, {0xe1, 0, 0x50}, {0xe0, 0x7f, 0x7f}, {0xb0, 0x01, 100}, {0xb1, 0x01, 20},
		{0xb0, 0x40, 127}, {0x81, 67, 10}, {0x90, 65, 60}, {0xb0, 0x40, 0}, {0xf2, 0x10, 0x01}, {0xf8}, {0xf8}, {0xfa}, {0xf8},
		{0xfb}, {0xfc}, {0x90, 48, 100}, {0x90, 50, 100}, {0x80, 48, 0}, {0xb0, 0x7b, 0}, {0x95, 61, 99}};
	for (const auto& bytes : messages) {
		member<&P::processMessage>(p, messageOf(bytes));
		record("[\"msg\", " + bytesOf(bytes) + "]");
		member<&P::processFilters>(p, 1.f / 1000.f);
		member<&P::processPulses>(p, 1.f / 4000.f);
		record("[\"step\", " + f(1.f / 1000.f) + ", " + f(1.f / 4000.f) + "]");
	}
	add(std::string("parser.") + name, {list(ops)}, list({list(out), parserJson(p)}));
}

/** One step of a script that calls the parser's methods and writes its fields directly, not through processMessage. */
struct ParserOp {
	std::string op;
	std::vector<int> args = {};
	std::string name = "";
	float x = 0.f;
};

template <uint8_t N>
void parserMethods(const char* name, const std::vector<ParserOp>& script) {
	using P = dsp::MidiParser<N>;
	P p;
	std::vector<std::string> ops, out;
	for (const ParserOp& s : script) {
		const std::vector<int>& a = s.args;
		std::string text;
		if (s.op == "assign") {
			// assignChannel returns the channel; record it with the state.
			uint8_t channel = member<&P::assignChannel>(p, (uint8_t) a[0]);
			ops.push_back("[\"assign\", " + i(a[0]) + "]");
			out.push_back(list({i(channel), parserState(p)}));
			continue;
		}
		if (s.op == "channels" || s.op == "rotate_index") {
			if (s.op == "channels")
				member<&P::setChannels>(p, (uint8_t) a[0]);
			else
				p.rotateIndex = (int8_t) a[0];
			text = i(a[0]);
		}
		else if (s.op == "poly" || s.op == "mono") {
			if (s.op == "poly")
				member<&P::setPolyMode>(p, (typename P::PolyMode) valueOf(POLY_MODES, s.name));
			else
				member<&P::setMonoMode>(p, (typename P::MonoMode) valueOf(MONO_MODES, s.name));
			text = str(s.name);
		}
		else if (s.op == "flags") {
			p.retriggerOnResume = a[0], p.releaseVelocityEnabled = a[1], p.clockDivision = (uint32_t) a[2];
			text = b(a[0]) + ", " + b(a[1]) + ", " + i(a[2]);
		}
		else if (s.op == "press" || s.op == "release") {
			if (s.op == "press")
				member<&P::pressNote>(p, (uint8_t) a[0], (uint8_t) a[1], (uint8_t) a[2]);
			else
				member<&P::releaseNote>(p, (uint8_t) a[0], (int8_t) a[1], (int8_t) a[2]);
			text = i(a[0]) + ", " + i(a[1]) + ", " + i(a[2]);
		}
		else if (s.op == "press_pedal")
			member<&P::pressPedal>(p);
		else if (s.op == "release_pedal")
			member<&P::releasePedal>(p);
		else if (s.op == "refresh")
			member<&P::refreshHeld>(p);
		else if (s.op == "pedal") {
			p.pedal = a[0];
			text = b(a[0]);
		}
		else if (s.op == "held") {
			p.heldNotes.assign(a.begin(), a.end());
			text = bytesOf(a);
		}
		else if (s.op == "cc" || s.op == "sys") {
			if (s.op == "cc")
				member<&P::processCC>(p, messageOf(a));
			else
				member<&P::processSystem>(p, messageOf(a));
			text = bytesOf(a);
		}
		else if (s.op == "lambda" || s.op == "pw_range") {
			if (s.op == "lambda")
				member<&P::setFilterLambda>(p, s.x);
			else
				p.pwRange = s.x;
			text = f(s.x);
		}
		else if (s.op == "step") {
			member<&P::processFilters>(p, 1.f / 1000.f);
			member<&P::processPulses>(p, 1.f / 4000.f);
			text = f(1.f / 1000.f) + ", " + f(1.f / 4000.f);
		}
		else {
			std::fprintf(stderr, "no parser op %s\n", s.op.c_str());
			std::abort();
		}
		ops.push_back("[" + str(s.op) + (text.empty() ? "" : ", " + text) + "]");
		out.push_back(parserState(p));
	}
	add(std::string("parser.") + name, {list(ops)}, list({list(out), parserJson(p)}));
}

void midiParsers() {
	add("parser.enums", {}, list({enumerators(MONO_MODES), enumerators(POLY_MODES)}));
	parser<16>("16.mono_last", 1, "ROTATE_MODE", "LAST_PRIORITY_MODE");
	parser<16>("16.mono_first", 1, "ROTATE_MODE", "FIRST_PRIORITY_MODE");
	parser<16>("16.mono_lowest", 1, "ROTATE_MODE", "LOWEST_PRIORITY_MODE");
	parser<16>("16.mono_highest", 1, "ROTATE_MODE", "HIGHEST_PRIORITY_MODE");
	parser<16>("16.rotate", 4, "ROTATE_MODE", "LAST_PRIORITY_MODE");
	parser<16>("16.reuse", 4, "REUSE_MODE", "LAST_PRIORITY_MODE");
	parser<16>("16.reset", 4, "RESET_MODE", "LAST_PRIORITY_MODE");
	parser<16>("16.mpe", 16, "MPE_MODE", "LAST_PRIORITY_MODE");
	parser<4>("4.rotate", 4, "ROTATE_MODE", "LAST_PRIORITY_MODE");
	parser<1>("1.mono", 1, "ROTATE_MODE", "LAST_PRIORITY_MODE");
	// The methods processMessage calls, called directly, and the fields written directly.
	parserMethods<16>("16.methods_poly", {{"channels", {4}}, {"poly", {}, "ROTATE_MODE"}, {"flags", {1, 1, 6}}, {"press", {60, 0, 100}},
		{"press", {64, 0, 90}}, {"assign", {67}}, {"press", {67, 3, 80}}, {"press_pedal"}, {"release", {60, 0, 33}},
		{"release_pedal"}, {"held", {64}}, {"refresh"}, {"release", {64, -1, -1}}, {"cc", {0xb0, 0x01, 100}},
		{"cc", {0xb0, 0x40, 127}}, {"release", {67, 0, 12}}, {"cc", {0xb0, 0x40, 0}}, {"sys", {0xf2, 0x10, 0x01}}, {"sys", {0xf8}},
		{"sys", {0xf8}}, {"sys", {0xfa}}, {"sys", {0xfb}}, {"sys", {0xfc}}, {"lambda", {}, "", 5.f}, {"step"}, {"step"},
		{"pw_range", {}, "", 7.5f}, {"cc", {0xb0, 0x7b, 1}}, {"rotate_index", {2}}, {"assign", {70}}, {"press", {70, 0, 64}},
		{"pedal", {1}}, {"release", {70, 0, 5}}, {"pedal", {0}}, {"poly", {}, "REUSE_MODE"}, {"press", {72, 0, 50}},
		{"assign", {72}}, {"assign", {74}}, {"poly", {}, "RESET_MODE"}, {"press", {48, 0, 30}}, {"assign", {50}},
		{"cc", {0xb0, 0x7b, 0}}});
	parserMethods<16>("16.methods_mono", {{"mono", {}, "LOWEST_PRIORITY_MODE"}, {"flags", {1, 0, 24}}, {"press", {62, 0, 100}},
		{"press", {60, 0, 90}}, {"press", {65, 0, 80}}, {"step"}, {"release", {60, 0, 20}}, {"held", {65, 62, 58}}, {"refresh"},
		{"assign", {70}}, {"mono", {}, "HIGHEST_PRIORITY_MODE"}, {"press", {50, 0, 40}}, {"press", {55, 0, 41}},
		{"press", {52, 0, 42}}, {"release", {55, -1, -1}}, {"mono", {}, "FIRST_PRIORITY_MODE"}, {"press", {40, 0, 1}},
		{"press", {41, 0, 2}}, {"held", {}}, {"refresh"}, {"step"}});
	parserMethods<16>("16.methods_mpe", {{"channels", {16}}, {"poly", {}, "MPE_MODE"}, {"flags", {0, 1, 3}}, {"press", {60, 5, 100}},
		{"press", {61, 9, 99}}, {"assign", {62}}, {"release", {61, 9, 40}}, {"cc", {0xb3, 0x01, 77}}, {"lambda", {}, "", 200.f},
		{"step"}, {"step"}, {"release", {60, 4, 10}}, {"release", {60, 5, -1}}});
	// from_json: Rack's keys, including the pre-2.5.3 lastPitch.
	dsp::MidiParser<16> p;
	json_t* j = json_loads("{\"channels\": 3, \"monoMode\": 2, \"retriggerOnResume\": true, \"polyMode\": 1, "
		"\"releaseVelocityEnabled\": true, \"pwRange\": 12.5, \"smooth\": false, \"clockDivision\": 12, \"lastPitch\": 10000, "
		"\"lastMod\": 64, \"filterLambda\": 5.0}", 0, nullptr);
	member<&dsp::MidiParser<16>::fromJson>(p, j);
	json_decref(j);
	add("parser.from_json", {}, list({parserState(p), i(p.monoMode), i(p.polyMode), b(p.retriggerOnResume),
		b(p.releaseVelocityEnabled), f(p.pwRange), b(p.smooth), i(p.clockDivision), f(p.pwFilters[0].lambda)}));
}

// --- fft.hpp and fir.hpp (RP-19A): PFFFT needs 16-byte-aligned buffers ----------------------

struct Aligned {
	float* data;
	explicit Aligned(size_t n) : data((float*) pffft_aligned_malloc(n * sizeof(float))) { std::memset(data, 0, n * sizeof(float)); }
	~Aligned() { pffft_aligned_free(data); }
};

std::vector<float> noise(size_t n, float amplitude) {
	std::vector<float> out(n);
	for (float& x : out)
		x = uniform(-amplitude, amplitude);
	return out;
}

void ffts() {
	for (int n : {32, 96, 160, 1024}) {
		using R = dsp::RealFFT;
		R fft(n);
		std::vector<float> x = noise(n, 1.f);
		for (int k = 0; k < n; k++)
			x[k] += std::sin(2 * (float) M_PI * 3 * k / n);  // a component at bin 3
		Aligned in(n), out(n), back(n), ordered(n), inverse(n);
		std::memcpy(in.data, x.data(), n * sizeof(float));
		member<&R::rfftUnordered>(fft, (const float*) in.data, out.data);
		member<&R::irfftUnordered>(fft, (const float*) out.data, back.data);
		member<&R::rfft>(fft, (const float*) in.data, ordered.data);
		member<&R::irfft>(fft, (const float*) ordered.data, inverse.data);
		std::vector<float> scaled(inverse.data, inverse.data + n);
		member<&R::scale>(fft, scaled.data());
		add("rfft." + std::to_string(n), {floats(x.data(), n)},
			list({floats(out.data, n), floats(back.data, n), floats(ordered.data, n), floats(inverse.data, n), floats(scaled.data(), n)}));
	}
	for (int n : {16, 48, 80, 512}) {
		using C = dsp::ComplexFFT;
		C fft(n);
		std::vector<float> x = noise(2 * n, 1.f);
		Aligned in(2 * n), out(2 * n), back(2 * n), ordered(2 * n), inverse(2 * n);
		std::memcpy(in.data, x.data(), 2 * n * sizeof(float));
		member<&C::fftUnordered>(fft, (const float*) in.data, out.data);
		member<&C::ifftUnordered>(fft, (const float*) out.data, back.data);
		member<&C::fft>(fft, (const float*) in.data, ordered.data);
		member<&C::ifft>(fft, (const float*) ordered.data, inverse.data);
		std::vector<float> scaled(inverse.data, inverse.data + 2 * n);
		member<&C::scale>(fft, scaled.data());
		add("cfft." + std::to_string(n), {floats(x.data(), 2 * n)}, list({floats(out.data, 2 * n), floats(back.data, 2 * n),
			floats(ordered.data, 2 * n), floats(inverse.data, 2 * n), floats(scaled.data(), 2 * n)}));
	}
}

void firs() {
	for (int len : {0, 1, 7, 64}) {
		std::vector<float> in = noise(std::max(len, 1), 2.f), kernel = noise(std::max(len, 1), 1.f);
		add("convolve_naive", {floats(in.data(), in.size()), floats(kernel.data(), kernel.size()), i(len)},
			f(calls::convolveNaive(in.data(), kernel.data(), opaque(len))));
	}
	for (int len : {0, 1, 5, 32})
		for (float cutoff : {0.5f, 0.1f, 0.25f}) {
			std::vector<float> out(std::max(len, 1), 7.f);
			calls::boxcarLowpassIR(out.data(), opaque(len), cutoff);
			add("boxcar_lowpass_ir", {i(len), f(cutoff)}, floats(out.data(), out.size()));
		}
	for (size_t block : {32, 64, 16})
		for (size_t length : {0, 1, 100, 200}) {
			using V = dsp::RealTimeConvolver;
			V conv(block);
			std::vector<float> kernel = noise(std::max<size_t>(length, 1), 1.f);
			const float* kp = length ? kernel.data() : nullptr;
			member<&V::setKernel>(conv, kp, length);
			std::vector<std::string> inputs, outputs;
			for (int k = 0; k < 8; k++) {
				std::vector<float> in = noise(block, 1.f), out(block);
				const float* ip = in.data();
				member<&V::processBlock>(conv, ip, out.data());
				inputs.push_back(floats(in.data(), block));
				outputs.push_back(floats(out.data(), block));
			}
			add("convolver", {i(block), floats(kernel.data(), length), list(inputs)},
				list({list(outputs), i(conv.kernelBlocks), i(conv.inputPos), floats(conv.inputFfts, block * 2 * conv.kernelBlocks)}));
		}
}

// --- minblep.hpp and resampler.hpp (RP-19B), constructed through calls::make as the bindings are --

void minBleps() {
	for (auto zo : std::vector<std::pair<int, int>>{{4, 4}, {16, 32}, {8, 16}}) {
		std::vector<float> out(2 * zo.first * zo.second);
		calls::minBlepImpulse(zo.first, zo.second, out.data());
		add("min_blep_impulse", {i(zo.first), i(zo.second)}, floats(out.data(), out.size()));
	}
	// A script: positive p inserts a discontinuity of x at p; "step" outputs a sample.
	std::vector<std::pair<float, float>> script = {{0.f, 1.f}, {-0.5f, -2.f}, {-1.f, 3.f}, {0.5f, 3.f}, {-0.25f, 1.f}, {NAN_, 1.f}, {-1e-3f, 0.5f}};
	{
		using G = dsp::MinBlepGenerator<16, 32, float>;
		std::unique_ptr<G> g(calls::make<G>());
		std::vector<std::string> ops, out;
		for (int k = 0; k < 40; k++) {
			if (k % 5 == 0) {
				auto [p, x] = script[(k / 5) % script.size()];
				member<&G::insertDiscontinuity>(*g, p, x);
				ops.push_back(list({f(p), f(x)}));
			}
			else
				ops.push_back("\"step\"");
			out.push_back(f(member<&G::process>(*g)));
		}
		add("minblep.16.32", {list(ops)}, list({list(out), i(g->pos)}));
	}
	{
		using G = dsp::MinBlepGenerator<8, 8, float_4>;
		std::unique_ptr<G> g(calls::make<G>());
		std::vector<std::string> ops, out;
		for (int k = 0; k < 24; k++) {
			if (k % 4 == 0) {
				auto [p, x] = script[(k / 4) % script.size()];
				float_4 v(x, -x, 2 * x, 0.f);
				member<&G::insertDiscontinuity>(*g, p, v);
				ops.push_back(list({f(p), f4(v)}));
			}
			else
				ops.push_back("\"step\"");
			out.push_back(f4(member<&G::process>(*g)));
		}
		add("minblep4.8.8", {list(ops)}, list({list(out), i(g->pos)}));
	}
}

/** With refreshIn and refreshOut, the rate fields are then assigned and the state rebuilt with refreshState. */
template <int N>
void converter(int inRate, int outRate, int quality, int channels, int frames, int outCapacity, int refreshIn = 0, int refreshOut = 0) {
	using S = dsp::SampleRateConverter<N>;
	std::unique_ptr<S> s(calls::make<S>());
	member<&S::setChannels>(*s, channels);
	member<&S::setQuality>(*s, quality);
	member<&S::setRates>(*s, inRate, outRate);
	if (refreshIn) {
		s->inRate = refreshIn, s->outRate = refreshOut;
		member<&S::refreshState>(*s);
	}
	std::vector<std::string> chunks;
	std::vector<std::string> inputs;
	for (int chunk = 0; chunk < 3; chunk++) {
		std::vector<float> in = noise((size_t) frames * N, 1.f);
		std::vector<float> out((size_t) outCapacity * N, 0.f);
		int inFrames = frames, outFrames = outCapacity;
		const float* ip = in.data();
		member<static_cast<void (S::*)(const float*, int, int*, float*, int, int*)>(&S::process)>(*s, ip, N, &inFrames, out.data(), N,
			&outFrames);
		inputs.push_back(floats(in.data(), in.size()));
		chunks.push_back(list({i(inFrames), i(outFrames), floats(out.data(), out.size())}));
	}
	std::vector<std::string> args = {i(inRate), i(outRate), i(quality), i(channels), i(frames), i(outCapacity), list(inputs)};
	if (refreshIn)
		args.push_back(i(refreshIn)), args.push_back(i(refreshOut));
	add("src." + std::to_string(N) + (refreshIn ? ".refresh" : ""), args, list(chunks));
}

template <int OS, int Q, typename T>
void decimator() {
	using D = dsp::Decimator<OS, Q, T>;
	std::unique_ptr<D> dec(calls::make<D>(0.9f));
	std::vector<std::string> ins, outs;
	for (int k = 0; k < 12; k++) {
		T in[OS];
		std::vector<std::string> enc4;
		for (int j = 0; j < OS; j++) {
			if constexpr (std::is_same<T, float>::value)
				in[j] = uniform(-1.f, 1.f);
			else
				in[j] = float_4(uniform(-1.f, 1.f), uniform(-1.f, 1.f), uniform(-1.f, 1.f), uniform(-1.f, 1.f));
			enc4.push_back(enc(in[j]));
		}
		T* p = in;
		outs.push_back(enc(member<&D::process>(*dec, p)));
		ins.push_back(list(enc4));
	}
	std::vector<std::string> inBuffer;
	for (int k = 0; k < OS * Q; k++)
		inBuffer.push_back(enc(dec->inBuffer[k]));
	add(std::string("decimator") + (std::is_same<T, float>::value ? "" : "4") + "." + std::to_string(OS) + "." + std::to_string(Q),
		{list(ins)}, list({list(outs), floats(dec->kernel, OS * Q), i(dec->inIndex), list(inBuffer)}));
}

template <int OS, int Q>
void upsampler() {
	using U = dsp::Upsampler<OS, Q>;
	std::unique_ptr<U> up(calls::make<U>(0.75f));
	std::vector<std::string> ins, outs;
	for (int k = 0; k < 12; k++) {
		float x = uniform(-1.f, 1.f);
		float out[OS];
		member<&U::process>(*up, x, (float*) out);
		ins.push_back(f(x));
		outs.push_back(floats(out, OS));
	}
	add("upsampler." + std::to_string(OS) + "." + std::to_string(Q), {list(ins)},
		list({list(outs), i(up->inIndex), floats(up->inBuffer, Q)}));
}

void resamplers() {
	converter<2>(44100, 48000, 4, 2, 64, 80);
	converter<2>(48000, 44100, 10, 2, 64, 80);
	converter<2>(48000, 48000, 4, 2, 64, 40);  // equal rates: Rack copies
	converter<1>(96000, 44100, 0, 1, 100, 50);
	converter<16>(44100, 88200, 4, 3, 32, 70);  // 3 of 16 channels
	converter<2>(44100, 48000, 4, 2, 64, 80, 48000, 32000);  // the fields changed, then refreshState
	decimator<4, 8, float>();
	decimator<8, 16, float_4>();
	upsampler<4, 8>();
	upsampler<8, 4>();
}

// --- ringbuffer.hpp (RP-19C): operation scripts, with the state after each step -----------

std::string u(size_t x) {
	return std::to_string((unsigned long long) x);
}

struct RingOp {
	std::string op;
	size_t n;
};

enum class Ring { plain, doubled, apple };

/** Runs ops on a fresh R (constructed as the bindings construct it), recording each step. */
template <Ring K, typename R, typename T>
void ringScript(const std::string& fn, const std::vector<RingOp>& ops) {
	constexpr size_t L = sizeof(T) / sizeof(float);
	std::unique_ptr<R> r(calls::make<R>());
	std::vector<std::string> steps;
	for (const RingOp& o : ops) {
		std::vector<std::string> step = {"\"" + o.op + "\""};
		std::string ret = "null";
		if (o.op == "push") {
			std::vector<float> x = noise(L, 1.f);
			T t;
			std::memcpy(&t, x.data(), sizeof(T));
			member<&R::push>(*r, t);
			step.push_back(floats(x.data(), L));
		}
		else if (o.op == "shift") {
			T t = member<&R::shift>(*r);
			ret = floats(reinterpret_cast<const float*>(&t), L);
		}
		else if (o.op == "clear") {
			if constexpr (K != Ring::apple)
				member<&R::clear>(*r);
		}
		else if (o.op == "return_buffer") {
			if constexpr (K == Ring::apple)
				member<&R::returnBuffer>(*r);
		}
		else if (o.op == "start_incr") {
			if constexpr (K != Ring::plain)
				member<&R::startIncr>(*r, o.n);
			step.push_back(u(o.n));
		}
		else if (o.op == "push_buffer") {
			std::vector<float> x = noise(L * o.n, 1.f);
			std::vector<T> tmp(o.n ? o.n : 1);
			if (o.n)
				std::memcpy(tmp.data(), x.data(), sizeof(T) * o.n);
			if constexpr (K == Ring::plain) {
				const T* p = tmp.data();
				member<&R::pushBuffer>(*r, p, (int) o.n);
			}
			step.push_back(floats(x.data(), x.size()));
			step.push_back(u(o.n));
		}
		else if (o.op == "shift_buffer") {
			std::vector<T> tmp(o.n ? o.n : 1);
			if constexpr (K == Ring::plain) {
				T* p = tmp.data();
				member<&R::shiftBuffer>(*r, p, o.n);
			}
			ret = floats(reinterpret_cast<const float*>(tmp.data()), L * o.n);
			step.push_back(u(o.n));
		}
		else if (o.op == "end_write") {
			std::vector<float> x = noise(L * o.n, 1.f);
			if constexpr (K == Ring::doubled) {
				T* p = member<&R::endData>(*r);
				if (o.n)
					std::memcpy(p, x.data(), sizeof(T) * o.n);
				member<&R::endIncr>(*r, o.n);
			}
			else if constexpr (K == Ring::apple) {
				T* p = member<&R::endData>(*r, o.n);
				if (o.n)
					std::memcpy(p, x.data(), sizeof(T) * o.n);
				member<&R::endIncr>(*r, o.n);
			}
			step.push_back(floats(x.data(), x.size()));
			step.push_back(u(o.n));
		}
		else if (o.op == "start_read") {
			if constexpr (K != Ring::plain) {
				const T* p = member<&R::startData>(static_cast<const R&>(*r));
				ret = floats(reinterpret_cast<const float*>(p), L * o.n);
				member<&R::startIncr>(*r, o.n);
			}
			step.push_back(u(o.n));
		}
		const R& c = *r;
		std::vector<std::string> state = {ret, u(member<&R::size>(c)), b(member<&R::empty>(c)), b(member<&R::full>(c)),
			u(member<&R::capacity>(c)), u(r->start), u(r->end)};
		steps.push_back(list({list(step), list(state)}));
	}
	size_t n = sizeof(r->data) / sizeof(T);
	add(fn, {u(n)}, list({list(steps), floats(reinterpret_cast<const float*>(r->data), L * n)}));
}

std::vector<RingOp> repeat(const std::string& op, int k, size_t n = 0) {
	return std::vector<RingOp>((size_t) k, RingOp{op, n});
}

std::vector<RingOp> concat(std::vector<std::vector<RingOp>> parts) {
	std::vector<RingOp> out;
	for (auto& part : parts)
		out.insert(out.end(), part.begin(), part.end());
	return out;
}

void ringBuffers() {
	// S = 16: wrap in both bulk copies, overrun (push when full), underrun (shift when empty), n = S and n = 0.
	auto plain = concat({repeat("push", 5), repeat("shift", 2), {{"push_buffer", 12}, {"shift_buffer", 10}}, repeat("push", 20),
		repeat("shift", 3), {{"clear", 0}, {"shift", 0}, {"push_buffer", 16}, {"shift_buffer", 16}, {"push_buffer", 0},
		{"shift_buffer", 0}, {"push_buffer", 7}, {"shift_buffer", 5}}});
	ringScript<Ring::plain, dsp::RingBuffer<float, 16>, float>("ring.float.16", plain);
	ringScript<Ring::plain, dsp::RingBuffer<float_4, 16>, float_4>("ring.float_4.16", plain);
	ringScript<Ring::plain, dsp::RingBuffer<dsp::Frame<2>, 16>, dsp::Frame<2>>("ring.Frame2.16", plain);
	// The doubled copy: appends through end_data that wrap, reads through start_data, overrun and underrun.
	auto doubled = concat({repeat("push", 5), repeat("shift", 2), {{"end_write", 14}, {"start_read", 8}}, repeat("push", 20),
		{{"clear", 0}, {"shift", 0}, {"start_incr", 3}, {"end_write", 16}, {"start_read", 16}, {"end_write", 9}, {"start_read", 4},
		{"end_write", 0}}});
	ringScript<Ring::doubled, dsp::DoubleRingBuffer<float, 16>, float>("doublering.float.16", doubled);
	ringScript<Ring::doubled, dsp::DoubleRingBuffer<float_4, 16>, float_4>("doublering.float_4.16", doubled);
	ringScript<Ring::doubled, dsp::DoubleRingBuffer<dsp::Frame<8>, 16>, dsp::Frame<8>>("doublering.Frame8.16", doubled);
	// N = 256: pushes and appends that reach the end of the block and move the window back, past S (Rack allows it).
	auto apple = concat({repeat("push", 5), repeat("shift", 2), {{"end_write", 100}, {"start_read", 90}}, repeat("push", 160),
		{{"start_read", 150}, {"return_buffer", 0}, {"end_write", 200}, {"start_read", 210}, {"end_write", 0}},
		repeat("push", 3), repeat("shift", 4)});
	ringScript<Ring::apple, dsp::AppleRingBuffer<float, 16, 256>, float>("applering.float.16.256", apple);
	ringScript<Ring::apple, dsp::AppleRingBuffer<float_4, 16, 256>, float_4>("applering.float_4.16.256", apple);
	ringScript<Ring::apple, dsp::AppleRingBuffer<dsp::Frame<16>, 16, 256>, dsp::Frame<16>>("applering.Frame16.16.256", apple);
}

}  // namespace

int main() {
	// Rack's main() sets flush-to-zero and denormals-are-zero (ADR 009); match it.
	system::resetFpuFlags();
	makeInputs();
	common();
	approx();
	conversions();
	windows();
	ode();
	digital();
	filters();
	vumeters();
	generator<16>();
	generator<4>();
	generator<1>();
	midiParsers();
	ffts();
	firs();
	minBleps();
	resamplers();
	ringBuffers();
	std::printf("{\"cases\": [\n");
	for (size_t k = 0; k < cases.size(); k++)
		std::printf("%s%s\n", cases[k].c_str(), k + 1 < cases.size() ? "," : "");
	std::printf("]}\n");
	return 0;
}
