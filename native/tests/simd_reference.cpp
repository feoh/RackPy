// SPDX-License-Identifier: GPL-3.0-or-later
// Differential reference for the SIMD bindings (RP-20): evaluates Rack's own
// rack::simd vectors, operators and functions and the sse_mathfun functions,
// lane by lane, over edge cases (signed zeros, denormals, infinities, NaN,
// integer extremes, masks) and seeded random inputs, compiled with the SDK's
// flags, and prints every case as JSON. tests/simd replays each case through
// rackpy.raw.simd inside Rack and compares the lanes bit for bit.
//
// CMake builds it twice: with the SDK's -march=nehalem (SSE4.2, Rack's x64
// implementation) and without SSE4.2 and with SIMDE_NO_NATIVE, so simd/common.hpp
// takes its SIMDe branch with SIMDe's portable code, as on a CPU without SSE.
// tests/simd/test_simde.py compares the two.
//
// Floats are written as their 32-bit patterns ("0x3f800000"), so NaN payloads,
// mask lanes, signed zeros and denormals survive the trip. Inputs pass through
// opaque() so the compiler cannot fold a case at compile time.
#include <rack.hpp>

#include <cinttypes>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace rack;
using simd::float_4;
using simd::int32_4;

#ifdef __SSE4_2__
const char* IMPLEMENTATION = "sse4.2";
#else
const char* IMPLEMENTATION = "simde";
#endif

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

int32_4 opaque(int32_4 x) {
	int32_t lanes[4];
	x.store(lanes);
	volatile int32_t* p = lanes;
	int32_t out[4] = {p[0], p[1], p[2], p[3]};
	return int32_4::load(out);
}

std::string f(float x) {
	uint32_t bits;
	std::memcpy(&bits, &x, sizeof bits);
	char buf[16];
	std::snprintf(buf, sizeof buf, "0x%08" PRIx32, bits);
	return std::string("{\"f\": \"") + buf + "\"}";
}

std::string i(long long x) {
	return std::to_string(x);
}

std::string b(bool x) {
	return x ? "true" : "false";
}

std::string f4(float_4 x) {
	std::string out = "{\"f4\": [";
	for (int k = 0; k < 4; k++) {
		uint32_t bits;
		std::memcpy(&bits, &x.s[k], sizeof bits);
		char buf[16];
		std::snprintf(buf, sizeof buf, "\"0x%08" PRIx32 "\"", bits);
		out += (k ? ", " : "") + std::string(buf);
	}
	return out + "]}";
}

std::string i4(int32_4 x) {
	std::string out = "{\"i4\": [";
	for (int k = 0; k < 4; k++)
		out += (k ? ", " : "") + std::to_string(x.s[k]);
	return out + "]}";
}

std::string list(const std::vector<std::string>& items) {
	std::string out = "[";
	for (size_t k = 0; k < items.size(); k++)
		out += (k ? ", " : "") + items[k];
	return out + "]";
}

std::vector<std::string> cases;

void add(const char* fn, const std::vector<std::string>& args, const std::string& result) {
	cases.push_back("{\"fn\": \"" + std::string(fn) + "\", \"args\": " + list(args) + ", \"result\": " + result + "}");
}

const float INF = std::numeric_limits<float>::infinity();
const float NAN_ = std::numeric_limits<float>::quiet_NaN();
const std::vector<float> EDGES = {0.f, -0.f, 1.f, -1.f, 0.5f, -0.5f, 1.5f, -1.5f, 2.5f, -2.5f, 3.5f, -3.5f, 1.f / 3.f, 0.1f,
	1e-7f, -1e-7f, 1e-45f, -1e-45f, 1.1754944e-38f, 3.4e38f, -3.4e38f, 100.f, -100.f, 2147483648.f, -2147483648.f,
	2147483520.f, 1e10f, 3.14159265f, -3.14159265f, 1.57079633f, 88.f, -88.f, INF, -INF, NAN_, -NAN_};
const std::vector<int32_t> INT_EDGES = {0, 1, -1, 2, -2, 3, 7, -7, 255, 1024, -1024, 0x55555555, -0x55555556, INT32_MAX,
	INT32_MIN, INT32_MAX - 1, INT32_MIN + 1, 0x7f800000, 0x3f800000, (int32_t) 0x80000000u + 1};

/** xorshift32, fixed seed: the same inputs on every run and every machine. */
uint32_t state = 0x9e3779b9u;
uint32_t next() {
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}
float uniform(float lo, float hi) {
	return lo + (hi - lo) * (float) (next() >> 8) / 16777216.f;
}

std::vector<float_4> FLOAT_VECTORS;
std::vector<int32_4> INT_VECTORS;

void makeInputs() {
	std::vector<float> lanes = EDGES;
	for (int k = 0; k < 40; k++)
		lanes.push_back(uniform(-10.f, 10.f));
	for (int k = 0; k < 24; k++)
		lanes.push_back(std::ldexp(uniform(-1.f, 1.f), (int) (next() % 120) - 60));
	while (lanes.size() % 4)
		lanes.push_back(uniform(-1.f, 1.f));
	for (size_t k = 0; k < lanes.size(); k += 4)
		FLOAT_VECTORS.push_back(float_4(lanes[k], lanes[k + 1], lanes[k + 2], lanes[k + 3]));
	// Shifted groupings, so each edge meets different neighbours, and every edge broadcast.
	for (size_t k = 2; k + 4 <= EDGES.size(); k += 4)
		FLOAT_VECTORS.push_back(float_4(EDGES[k], EDGES[k + 1], EDGES[k + 2], EDGES[k + 3]));
	for (float e : EDGES)
		FLOAT_VECTORS.push_back(float_4(e));

	std::vector<int32_t> ints = INT_EDGES;
	for (int k = 0; k < 28; k++)
		ints.push_back((int32_t) next());
	for (int k = 0; k < 8; k++)
		ints.push_back((int32_t) (next() % 2001) - 1000);
	while (ints.size() % 4)
		ints.push_back((int32_t) next());
	for (size_t k = 0; k < ints.size(); k += 4)
		INT_VECTORS.push_back(int32_4(ints[k], ints[k + 1], ints[k + 2], ints[k + 3]));
	for (int32_t e : INT_EDGES)
		INT_VECTORS.push_back(int32_4(e));
}

/** Pairs of inputs: every input with a few partners, and every broadcast edge with every other. */
template <typename V>
std::vector<std::pair<V, V>> pairs(const std::vector<V>& vs, size_t broadcastFrom) {
	std::vector<std::pair<V, V>> out;
	size_t n = vs.size();
	for (size_t k = 0; k < n; k++) {
		out.push_back({vs[k], vs[(k * 7 + 3) % n]});
		out.push_back({vs[(k * 5 + 1) % n], vs[k]});
	}
	for (size_t a = broadcastFrom; a < n; a += 3)
		for (size_t c = broadcastFrom; c < n; c += 2)
			out.push_back({vs[a], vs[c]});
	return out;
}

/** Masks a comparison might produce: per-lane all-ones or all-zeros, every pattern. */
std::vector<float_4> masks() {
	std::vector<float_4> out;
	for (int m = 0; m < 16; m++)
		out.push_back(simd::movemaskInverse<float_4>(m));
	return out;
}

#define UNARY_F4(name, expr) \
	for (float_4 x : FLOAT_VECTORS) { \
		float_4 a = opaque(x); \
		add(name, {f4(x)}, f4(expr)); \
	}
#define BINARY_F4(name, expr) \
	for (auto& p : fpairs) { \
		float_4 a = opaque(p.first), c = opaque(p.second); \
		add(name, {f4(p.first), f4(p.second)}, f4(expr)); \
	}
#define UNARY_I4(name, expr) \
	for (int32_4 x : INT_VECTORS) { \
		int32_4 a = opaque(x); \
		add(name, {i4(x)}, i4(expr)); \
	}
#define BINARY_I4(name, expr) \
	for (auto& p : ipairs) { \
		int32_4 a = opaque(p.first), c = opaque(p.second); \
		add(name, {i4(p.first), i4(p.second)}, i4(expr)); \
	}
#define UNARY_F(name, expr) \
	for (float x : EDGES) { \
		float a = opaque(x); \
		add(name, {f(x)}, f(expr)); \
	}
#define BINARY_F(name, expr) \
	for (float x : EDGES) \
		for (float y : {0.f, -0.f, 1.f, -2.5f, 0.5f, 3.5f, INF, NAN_}) { \
			float a = opaque(x), c = opaque(y); \
			add(name, {f(x), f(y)}, f(expr)); \
		}

void vectors() {
	// Construction, conversion and casts.
	for (float x : EDGES)
		add("f4.broadcast", {f(x)}, f4(float_4(opaque(x))));
	for (int32_t x : INT_EDGES)
		add("i4.broadcast", {i(x)}, i4(int32_4(opaque(x))));
	for (float_4 x : FLOAT_VECTORS) {
		float_4 a = opaque(x);
		add("f4.lanes", {f(x.s[0]), f(x.s[1]), f(x.s[2]), f(x.s[3])}, f4(float_4(a.s[0], a.s[1], a.s[2], a.s[3])));
		add("i4.from_f4", {f4(x)}, i4(int32_4(a)));
		add("i4.cast", {f4(x)}, i4(int32_4::cast(a)));
		float out[4];
		a.store(out);
		add("f4.store", {f4(x)}, list({f(out[0]), f(out[1]), f(out[2]), f(out[3])}));
	}
	for (int32_4 x : INT_VECTORS) {
		int32_4 a = opaque(x);
		add("f4.from_i4", {i4(x)}, f4(float_4(a)));
		add("f4.cast", {i4(x)}, f4(float_4::cast(a)));
		int32_t out[4];
		a.store(out);
		add("i4.store", {i4(x)}, list({i(out[0]), i(out[1]), i(out[2]), i(out[3])}));
	}
	add("f4.zero", {}, f4(float_4::zero()));
	add("f4.mask", {}, f4(float_4::mask()));
	add("i4.zero", {}, i4(int32_4::zero()));
	add("i4.mask", {}, i4(int32_4::mask()));
	float loadIn[5] = {1.f, -2.f, 3.5f, NAN_, 9.f};
	add("f4.load", {list({f(loadIn[0]), f(loadIn[1]), f(loadIn[2]), f(loadIn[3])})}, f4(float_4::load(loadIn)));
	int32_t loadInts[4] = {5, -6, INT32_MAX, INT32_MIN};
	add("i4.load", {list({i(loadInts[0]), i(loadInts[1]), i(loadInts[2]), i(loadInts[3])})}, i4(int32_4::load(loadInts)));
}

void operators() {
	auto fpairs = pairs(FLOAT_VECTORS, FLOAT_VECTORS.size() - EDGES.size());
	auto ipairs = pairs(INT_VECTORS, INT_VECTORS.size() - INT_EDGES.size());
	BINARY_F4("f4.add", a + c)
	BINARY_F4("f4.sub", a - c)
	BINARY_F4("f4.mul", a * c)
	BINARY_F4("f4.div", a / c)
	BINARY_F4("f4.xor", a ^ c)
	BINARY_F4("f4.and", a & c)
	BINARY_F4("f4.or", a | c)
	BINARY_F4("f4.eq", a == c)
	BINARY_F4("f4.ge", a >= c)
	BINARY_F4("f4.gt", a > c)
	BINARY_F4("f4.le", a <= c)
	BINARY_F4("f4.lt", a < c)
	BINARY_F4("f4.ne", a != c)
	BINARY_F4("f4.iadd", (a += c))
	BINARY_F4("f4.isub", (a -= c))
	BINARY_F4("f4.imul", (a *= c))
	BINARY_F4("f4.idiv", (a /= c))
	BINARY_F4("f4.ixor", (a ^= c))
	BINARY_F4("f4.iand", (a &= c))
	BINARY_F4("f4.ior", (a |= c))
	UNARY_F4("f4.pos", +a)
	UNARY_F4("f4.neg", -a)
	UNARY_F4("f4.invert", ~a)
	UNARY_F4("f4.pre_increment", ++a)
	UNARY_F4("f4.pre_decrement", --a)
	for (float_4 x : FLOAT_VECTORS) {
		float_4 a = opaque(x);
		float_4 old = a++;
		add("f4.post_increment", {f4(x)}, list({f4(old), f4(a)}));
		a = opaque(x);
		old = a--;
		add("f4.post_decrement", {f4(x)}, list({f4(old), f4(a)}));
	}
	// Mixed with a scalar, as in Rack's own `2.f * a`: the scalar is broadcast.
	for (float_4 x : FLOAT_VECTORS)
		for (float s : {2.f, -0.5f, 0.f, INF, NAN_}) {
			float_4 a = opaque(x);
			float t = opaque(s);
			add("f4.rsub", {f4(x), f(s)}, f4(t - a));
			add("f4.rdiv", {f4(x), f(s)}, f4(t / a));
			add("f4.mul_s", {f4(x), f(s)}, f4(a * t));
		}

	BINARY_I4("i4.add", a + c)
	BINARY_I4("i4.sub", a - c)
	BINARY_I4("i4.xor", a ^ c)
	BINARY_I4("i4.and", a & c)
	BINARY_I4("i4.or", a | c)
	BINARY_I4("i4.eq", a == c)
	BINARY_I4("i4.ge", a >= c)
	BINARY_I4("i4.gt", a > c)
	BINARY_I4("i4.le", a <= c)
	BINARY_I4("i4.lt", a < c)
	BINARY_I4("i4.ne", a != c)
	BINARY_I4("i4.iadd", (a += c))
	BINARY_I4("i4.isub", (a -= c))
	BINARY_I4("i4.ixor", (a ^= c))
	BINARY_I4("i4.iand", (a &= c))
	BINARY_I4("i4.ior", (a |= c))
	UNARY_I4("i4.pos", +a)
	UNARY_I4("i4.neg", -a)
	UNARY_I4("i4.invert", ~a)
	UNARY_I4("i4.pre_increment", ++a)
	UNARY_I4("i4.pre_decrement", --a)
	for (int32_4 x : INT_VECTORS) {
		int32_4 a = opaque(x);
		int32_4 old = a++;
		add("i4.post_increment", {i4(x)}, list({i4(old), i4(a)}));
		a = opaque(x);
		old = a--;
		add("i4.post_decrement", {i4(x)}, list({i4(old), i4(a)}));
		for (int s : {0, 1, 5, 23, 31, 32, 33, 100, -1, INT32_MAX, INT32_MIN}) {
			int32_4 v = opaque(x);
			int t = opaque(s);
			add("i4.lshift", {i4(x), i(s)}, i4(v << t));
			add("i4.rshift", {i4(x), i(s)}, i4(v >> t));
		}
	}
}

void functions() {
	auto fpairs = pairs(FLOAT_VECTORS, FLOAT_VECTORS.size() - EDGES.size());
	std::vector<float_4> ms = masks();
	BINARY_F4("andnot", simd::andnot(a, c))
	for (int32_4 x : INT_VECTORS)
		add("movemask_i4", {i4(x)}, i(simd::movemask(opaque(x))));
	for (float_4 x : FLOAT_VECTORS)
		add("movemask_f", {f4(x)}, i(simd::movemask(opaque(x))));
	UNARY_F4("rsqrt", simd::rsqrt(a))
	UNARY_F4("rcp", simd::rcp(a))
	for (bool cond : {true, false})
		for (float x : EDGES)
			add("ifelse_b", {b(cond), f(x), f(2.5f)}, f(simd::ifelse(opaque(cond), opaque(x), 2.5f)));
	for (float_4 m : ms)
		for (size_t k = 0; k < FLOAT_VECTORS.size(); k += 3) {
			float_4 x = FLOAT_VECTORS[k], y = FLOAT_VECTORS[(k * 11 + 5) % FLOAT_VECTORS.size()];
			add("ifelse_f4", {f4(m), f4(x), f4(y)}, f4(simd::ifelse(opaque(m), opaque(x), opaque(y))));
		}
	// A non-mask "mask": ifelse is bitwise, so arbitrary bits mix the two inputs.
	for (size_t k = 0; k < FLOAT_VECTORS.size(); k++) {
		float_4 m = FLOAT_VECTORS[k], x = FLOAT_VECTORS[(k + 1) % FLOAT_VECTORS.size()], y = FLOAT_VECTORS[(k + 2) % FLOAT_VECTORS.size()];
		add("ifelse_f4", {f4(m), f4(x), f4(y)}, f4(simd::ifelse(opaque(m), opaque(x), opaque(y))));
	}
	for (int m : {0, 1, 2, 5, 10, 15, 16, 31, -1, INT32_MAX, INT32_MIN}) {
		add("movemask_inverse_i4", {i(m)}, i4(simd::movemaskInverse<int32_4>(opaque(m))));
		add("movemask_inverse_f4", {i(m)}, f4(simd::movemaskInverse<float_4>(opaque(m))));
	}

	BINARY_F4("fmax", simd::fmax(a, c))
	BINARY_F4("fmin", simd::fmin(a, c))
	UNARY_F4("sqrt", simd::sqrt(a))
	UNARY_F4("log", simd::log(a))
	UNARY_F4("log10", simd::log10(a))
	UNARY_F4("log2", simd::log2(a))
	UNARY_F4("exp", simd::exp(a))
	UNARY_F4("sin", simd::sin(a))
	UNARY_F4("cos", simd::cos(a))
	UNARY_F4("tan", simd::tan(a))
	UNARY_F4("atan", simd::atan(a))
	BINARY_F4("atan2", simd::atan2(a, c))
	UNARY_F4("trunc", simd::trunc(a))
	UNARY_F4("floor", simd::floor(a))
	UNARY_F4("ceil", simd::ceil(a))
	UNARY_F4("round", simd::round(a))
	BINARY_F4("fmod", simd::fmod(a, c))
	BINARY_F4("hypot", simd::hypot(a, c))
	UNARY_F4("fabs", simd::fabs(a))
	UNARY_F4("abs", simd::abs(a))
	BINARY_F4("abs_c", simd::abs(std::complex<float_4>(a, c)))
	BINARY_F4("arg_c", simd::arg(std::complex<float_4>(a, c)))
	BINARY_F4("pow", simd::pow(a, c))
	for (float s : {0.f, 1.f, 2.f, 10.f, 0.5f, -2.f, INF, NAN_})
		for (float_4 x : FLOAT_VECTORS)
			add("pow_s", {f(s), f4(x)}, f4(simd::pow(opaque(s), opaque(x))));
	for (int e : {-3, -1, 0, 1, 2, 3, 5, 8, 13, 31, 1000, (1 << 30) - 1})
		for (float_4 x : FLOAT_VECTORS) {
			add("pow_i", {f4(x), i(e)}, f4(simd::pow(opaque(x), opaque(e))));
			add("pow_i_f", {f(x.s[0]), i(e)}, f(simd::pow(opaque(x.s[0]), opaque(e))));
		}
	for (size_t k = 0; k < fpairs.size(); k += 2) {
		float_4 x = opaque(fpairs[k].first), lo = opaque(fpairs[k].second), hi = opaque(fpairs[(k + 1) % fpairs.size()].first);
		add("clamp", {f4(fpairs[k].first), f4(fpairs[k].second), f4(fpairs[(k + 1) % fpairs.size()].first)}, f4(simd::clamp(x, lo, hi)));
		float_4 y0 = opaque(fpairs[(k + 3) % fpairs.size()].second), y1 = opaque(fpairs[(k + 5) % fpairs.size()].first);
		add("rescale", {f4(x), f4(lo), f4(hi), f4(y0), f4(y1)}, f4(simd::rescale(x, lo, hi, y0, y1)));
		add("crossfade", {f4(x), f4(lo), f4(hi)}, f4(simd::crossfade(x, lo, hi)));
	}
	UNARY_F4("clamp_default", simd::clamp(a))
	UNARY_F4("sgn", simd::sgn(a))

	// The std:: and math:: scalar functions simd:: imports, for T = float.
	BINARY_F("fmax_f", simd::fmax(a, c))
	BINARY_F("fmin_f", simd::fmin(a, c))
	UNARY_F("sqrt_f", simd::sqrt(a))
	UNARY_F("log_f", simd::log(a))
	UNARY_F("log10_f", simd::log10(a))
	UNARY_F("log2_f", simd::log2(a))
	UNARY_F("exp_f", simd::exp(a))
	UNARY_F("sin_f", simd::sin(a))
	UNARY_F("cos_f", simd::cos(a))
	UNARY_F("tan_f", simd::tan(a))
	UNARY_F("atan_f", simd::atan(a))
	BINARY_F("atan2_f", simd::atan2(a, c))
	UNARY_F("trunc_f", simd::trunc(a))
	UNARY_F("floor_f", simd::floor(a))
	UNARY_F("ceil_f", simd::ceil(a))
	UNARY_F("round_f", simd::round(a))
	BINARY_F("fmod_f", simd::fmod(a, c))
	BINARY_F("hypot_f", simd::hypot(a, c))
	UNARY_F("fabs_f", simd::fabs(a))
	UNARY_F("abs_f", simd::abs(a))
	BINARY_F("pow_f", simd::pow(a, c))
	UNARY_F("sgn_f", simd::sgn(a))
	for (float x : EDGES)
		for (float y : {0.5f, -1.f, INF}) {
			add("clamp_f", {f(x), f(-1.f), f(y)}, f(simd::clamp(opaque(x), -1.f, opaque(y))));
			add("rescale_f", {f(x), f(0.f), f(y), f(-1.f), f(1.f)}, f(simd::rescale(opaque(x), 0.f, opaque(y), -1.f, 1.f)));
			add("crossfade_f", {f(x), f(y), f(0.25f)}, f(simd::crossfade(opaque(x), opaque(y), 0.25f)));
		}
	for (float x : EDGES) {
		std::complex<float> z(opaque(x), opaque(2.f));
		add("abs_cf", {f(x), f(2.f)}, f(simd::abs(z)));
		add("arg_cf", {f(x), f(2.f)}, f(simd::arg(z)));
	}
}

void sseMathfun() {
	auto fpairs = pairs(FLOAT_VECTORS, FLOAT_VECTORS.size() - EDGES.size());
	add("sse_mathfun_one_ps", {}, f4(float_4(sse_mathfun_one_ps())));
	UNARY_F4("sse_mathfun_log_ps", float_4(sse_mathfun_log_ps(a.v)))
	UNARY_F4("sse_mathfun_exp_ps", float_4(sse_mathfun_exp_ps(a.v)))
	UNARY_F4("sse_mathfun_sin_ps", float_4(sse_mathfun_sin_ps(a.v)))
	UNARY_F4("sse_mathfun_cos_ps", float_4(sse_mathfun_cos_ps(a.v)))
	UNARY_F4("sse_mathfun_tan_ps", float_4(sse_mathfun_tan_ps(a.v)))
	UNARY_F4("sse_mathfun_cot_ps", float_4(sse_mathfun_cot_ps(a.v)))
	UNARY_F4("sse_mathfun_atan_ps", float_4(sse_mathfun_atan_ps(a.v)))
	BINARY_F4("sse_mathfun_atan2_ps", float_4(sse_mathfun_atan2_ps(a.v, c.v)))
	for (float_4 x : FLOAT_VECTORS) {
		__m128 s, c;
		sse_mathfun_sincos_ps(opaque(x).v, &s, &c);
		add("sse_mathfun_sincos_ps", {f4(x)}, list({f4(float_4(s)), f4(float_4(c))}));
		for (int flag : {0, 1, 2, -1})
			add("sse_mathfun_tancot_ps", {f4(x), i(flag)}, f4(float_4(sse_mathfun_tancot_ps(opaque(x).v, opaque(flag)))));
	}
}

}  // namespace

int main() {
	// Rack's main() sets flush-to-zero and denormals-are-zero, so every float
	// operation inside Rack treats subnormals as zero; match it (ADR 009).
	system::resetFpuFlags();
	makeInputs();
	vectors();
	operators();
	functions();
	sseMathfun();
	std::printf("{\"implementation\": \"%s\", \"cases\": [\n", IMPLEMENTATION);
	for (size_t k = 0; k < cases.size(); k++)
		std::printf("%s%s\n", cases[k].c_str(), k + 1 < cases.size() ? "," : "");
	std::printf("]}\n");
	return 0;
}
