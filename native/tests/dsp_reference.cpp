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

#include <cinttypes>
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
	std::printf("{\"cases\": [\n");
	for (size_t k = 0; k < cases.size(); k++)
		std::printf("%s%s\n", cases[k].c_str(), k + 1 < cases.size() ? "," : "");
	std::printf("]}\n");
	return 0;
}
