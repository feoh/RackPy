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
			list(out));
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
void biquads(const char* suffix) {
	using F = dsp::TBiquadFilter<T>;
	using Base = dsp::IIRFilter<3, 3, T>;  // the bindings call the inherited methods on the base, as here
	{
		F fresh;
		Base& base = fresh;
		add(std::string("biquad_default") + suffix, {}, list({enc(fresh.b[0]), enc(fresh.a[0]), enc(member<&Base::process>(base, T(1.f)))}));
	}
	for (int type = 0; type <= (int) F::NUM_TYPES; type++)
		for (auto p : std::vector<std::array<float, 3>>{{0.1f, 0.707f, 1.f}, {0.01f, 2.f, 4.f}, {0.3f, 0.5f, 0.25f}, {0.49f, 10.f, 1.f}}) {
			F filter;
			member<&F::setParameters>(filter, (typename F::Type) type, p[0], p[1], p[2]);
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
			add(std::string("biquad") + suffix, {i(type), f(p[0]), f(p[1]), f(p[2]), list(frequencies)},
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
		out.push_back(list(msgs));
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
	std::vector<std::string> notes, gates, vels, afts, pws, mods, retrig, pitch, held, wheels;
	for (int c = 0; c < N; c++) {
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
		list(retrig)});
}

template <uint8_t N>
void parser(const char* name, int channels, int poly, int mono) {
	using P = dsp::MidiParser<N>;
	P p;
	std::vector<std::string> ops, out;
	auto record = [&](const std::string& op) {
		ops.push_back(op);
		out.push_back(parserState(p));
	};
	member<&P::setChannels>(p, (uint8_t) channels);
	record("[\"channels\", " + i(channels) + "]");
	member<&P::setPolyMode>(p, (typename P::PolyMode) poly);
	record("[\"poly\", " + i(poly) + "]");
	member<&P::setMonoMode>(p, (typename P::MonoMode) mono);
	record("[\"mono\", " + i(mono) + "]");
	p.retriggerOnResume = true;
	p.releaseVelocityEnabled = true;
	p.clockDivision = 6;
	record("[\"flags\", true, true, 6]");
	std::vector<std::vector<int>> messages = {{0x90, 60, 100}, {0x91, 64, 90}, {0x92, 67, 80}, {0x90, 72, 70}, {0x93, 60, 0},
		{0x80, 64, 33}, {0xa0, 67, 50}, {0xd2, 77}, {0xe1, 0, 0x50}, {0xe0, 0x7f, 0x7f}, {0xb0, 0x01, 100}, {0xb1, 0x01, 20},
		{0xb0, 0x40, 127}, {0x81, 67, 10}, {0x90, 65, 60}, {0xb0, 0x40, 0}, {0xf2, 0x10, 0x01}, {0xf8}, {0xf8}, {0xfa}, {0xf8},
		{0xfb}, {0xfc}, {0x90, 48, 100}, {0x90, 50, 100}, {0x80, 48, 0}, {0xb0, 0x7b, 0}, {0x95, 61, 99}};
	for (const auto& bytes : messages) {
		midi::Message m;
		m.bytes.assign(bytes.begin(), bytes.end());
		m.frame = 7;
		member<&P::processMessage>(p, m);
		std::vector<std::string> bs;
		for (int x : bytes)
			bs.push_back(i(x));
		record("[\"msg\", " + list(bs) + "]");
		member<&P::processFilters>(p, 1.f / 1000.f);
		member<&P::processPulses>(p, 1.f / 4000.f);
		record("[\"step\", " + f(1.f / 1000.f) + ", " + f(1.f / 4000.f) + "]");
	}
	json_t* j = member<&P::toJson>(p);
	char* text = json_dumps(j, JSON_SORT_KEYS | JSON_COMPACT);
	json_decref(j);
	std::string json = text;
	std::free(text);
	add(std::string("parser.") + name, {list(ops)}, list({list(out), "\"" + [&] {
		std::string esc;
		for (char ch : json)
			esc += (ch == '"' || ch == '\\') ? std::string("\\") + ch : std::string(1, ch);
		return esc;
	}() + "\""}));
}

void midiParsers() {
	parser<16>("16.mono_last", 1, 0, 0);
	parser<16>("16.mono_first", 1, 0, 1);
	parser<16>("16.mono_lowest", 1, 0, 2);
	parser<16>("16.mono_highest", 1, 0, 3);
	parser<16>("16.rotate", 4, 0, 0);
	parser<16>("16.reuse", 4, 1, 0);
	parser<16>("16.reset", 4, 2, 0);
	parser<16>("16.mpe", 16, 3, 0);
	parser<4>("4.rotate", 4, 0, 0);
	parser<1>("1.mono", 1, 0, 0);
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
				list({list(outputs), i(conv.kernelBlocks), i(conv.inputPos)}));
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

template <int N>
void converter(int inRate, int outRate, int quality, int channels, int frames, int outCapacity) {
	using S = dsp::SampleRateConverter<N>;
	std::unique_ptr<S> s(calls::make<S>());
	member<&S::setChannels>(*s, channels);
	member<&S::setQuality>(*s, quality);
	member<&S::setRates>(*s, inRate, outRate);
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
	add("src." + std::to_string(N), {i(inRate), i(outRate), i(quality), i(channels), i(frames), i(outCapacity), list(inputs)},
		list(chunks));
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
	add(std::string("decimator") + (std::is_same<T, float>::value ? "" : "4") + "." + std::to_string(OS) + "." + std::to_string(Q),
		{list(ins)}, list({list(outs), floats(dec->kernel, OS * Q), i(dec->inIndex)}));
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
	add("upsampler." + std::to_string(OS) + "." + std::to_string(Q), {list(ins)}, list({list(outs), i(up->inIndex)}));
}

void resamplers() {
	converter<2>(44100, 48000, 4, 2, 64, 80);
	converter<2>(48000, 44100, 10, 2, 64, 80);
	converter<2>(48000, 48000, 4, 2, 64, 40);  // equal rates: Rack copies
	converter<1>(96000, 44100, 0, 1, 100, 50);
	converter<16>(44100, 88200, 4, 3, 32, 70);  // 3 of 16 channels
	decimator<4, 8, float>();
	decimator<8, 16, float_4>();
	upsampler<4, 8>();
	upsampler<8, 4>();
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
	std::printf("{\"cases\": [\n");
	for (size_t k = 0; k < cases.size(); k++)
		std::printf("%s%s\n", cases[k].c_str(), k + 1 < cases.size() ? "," : "");
	std::printf("]}\n");
	return 0;
}
