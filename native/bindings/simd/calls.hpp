// SPDX-License-Identifier: GPL-3.0-or-later
// Out-of-line calls to Rack's rack::simd functions and sse_mathfun, shared by
// the bindings (functions.cpp, sse_mathfun.cpp) and the C++ reference
// (native/tests/simd_reference.cpp).
//
// Rack compiles with -funsafe-math-optimizations, which lets the compiler
// reassociate and contract composite arithmetic differently wherever an inline
// function is inlined: the same Rack function, inlined into two call sites, can
// round differently in the last bits, and lose hypot's overflow protection in
// one of them (GCC 13 against GCC 16 showed both). Each wrapper is one
// non-inlined, non-IPA function per Rack overload, so the reference and the
// bindings run the same machine code and the comparison checks what it can:
// that a Python call reaches the right Rack function with the right arguments.
// Single instructions (the vector operators, conversions) need no wrapper.
#pragma once
#include <rack.hpp>

#include <complex>

#ifndef RACKPY_OUT_OF_LINE
#if defined(__clang__)
// Clang (macOS, RP-40) has no noipa; noinline keeps each call out of line, though Clang may still
// specialise across it.
#define RACKPY_OUT_OF_LINE [[gnu::noinline]] inline
#else
#define RACKPY_OUT_OF_LINE [[gnu::noipa]] inline
#endif
#endif

namespace rackpy {
namespace simd_calls {

using rack::simd::float_4;
using rack::simd::int32_4;
namespace s = rack::simd;

// One argument: the std:: or math:: float overload simd imports, and Rack's float_4 one.
#define RACKPY_SIMD_1(name) \
	RACKPY_OUT_OF_LINE float name(float x) { return s::name(x); } \
	RACKPY_OUT_OF_LINE float_4 name(float_4 x) { return s::name(x); }
#define RACKPY_SIMD_2(name) \
	RACKPY_OUT_OF_LINE float name(float a, float b) { return s::name(a, b); } \
	RACKPY_OUT_OF_LINE float_4 name(float_4 a, float_4 b) { return s::name(a, b); }

RACKPY_SIMD_2(fmax)
RACKPY_SIMD_2(fmin)
RACKPY_SIMD_1(sqrt)
RACKPY_SIMD_1(log)
RACKPY_SIMD_1(log10)
RACKPY_SIMD_1(log2)
RACKPY_SIMD_1(exp)
RACKPY_SIMD_1(sin)
RACKPY_SIMD_1(cos)
RACKPY_SIMD_1(tan)
RACKPY_SIMD_1(atan)
RACKPY_SIMD_2(atan2)
RACKPY_SIMD_1(trunc)
RACKPY_SIMD_1(floor)
RACKPY_SIMD_1(ceil)
RACKPY_SIMD_1(round)
RACKPY_SIMD_2(fmod)
RACKPY_SIMD_2(hypot)
RACKPY_SIMD_1(fabs)
RACKPY_SIMD_1(abs)
RACKPY_SIMD_1(sgn)
#undef RACKPY_SIMD_1
#undef RACKPY_SIMD_2

RACKPY_OUT_OF_LINE float_4 andnot(float_4 a, float_4 b) { return s::andnot(a, b); }
RACKPY_OUT_OF_LINE int movemask(float_4 a) { return s::movemask(a); }
RACKPY_OUT_OF_LINE int movemask(int32_4 a) { return s::movemask(a); }
RACKPY_OUT_OF_LINE float_4 rsqrt(float_4 x) { return s::rsqrt(x); }
RACKPY_OUT_OF_LINE float_4 rcp(float_4 x) { return s::rcp(x); }
RACKPY_OUT_OF_LINE float ifelse(bool cond, float a, float b) { return s::ifelse(cond, a, b); }
RACKPY_OUT_OF_LINE float_4 ifelse(float_4 mask, float_4 a, float_4 b) { return s::ifelse(mask, a, b); }
RACKPY_OUT_OF_LINE int32_4 movemaskInverseInt(int a) { return s::movemaskInverse<int32_4>(a); }
RACKPY_OUT_OF_LINE float_4 movemaskInverseFloat(int a) { return s::movemaskInverse<float_4>(a); }
RACKPY_OUT_OF_LINE float abs(std::complex<float> z) { return s::abs(z); }
RACKPY_OUT_OF_LINE float arg(std::complex<float> z) { return s::arg(z); }
RACKPY_OUT_OF_LINE float_4 abs(std::complex<float_4> z) { return s::abs(z); }
RACKPY_OUT_OF_LINE float_4 arg(std::complex<float_4> z) { return s::arg(z); }
RACKPY_OUT_OF_LINE float pow(float a, float b) { return s::pow(a, b); }
RACKPY_OUT_OF_LINE float_4 pow(float a, float_4 b) { return s::pow(a, b); }
RACKPY_OUT_OF_LINE float_4 pow(float_4 a, float_4 b) { return s::pow(a, b); }
RACKPY_OUT_OF_LINE float powInt(float a, int b) { return s::pow<float>(a, b); }
// Deduced, not s::pow<float_4>: rack::simd has `using std::pow`, and naming float_4 explicitly makes
// libc++ (macOS) substitute it into std::pow(complex<T>, U), whose __promote is a hard error. Deduction
// still picks Rack's pow(T, int): std::pow's overloads do not deduce from a float_4.
RACKPY_OUT_OF_LINE float_4 powInt(float_4 a, int b) { return s::pow(a, b); }
RACKPY_OUT_OF_LINE float clamp(float x, float a, float b) { return s::clamp(x, a, b); }
RACKPY_OUT_OF_LINE float_4 clamp(float_4 x, float_4 a, float_4 b) { return s::clamp(x, a, b); }
RACKPY_OUT_OF_LINE float rescale(float x, float xMin, float xMax, float yMin, float yMax) {
	return s::rescale(x, xMin, xMax, yMin, yMax);
}
RACKPY_OUT_OF_LINE float_4 rescale(float_4 x, float_4 xMin, float_4 xMax, float_4 yMin, float_4 yMax) {
	return s::rescale(x, xMin, xMax, yMin, yMax);
}
RACKPY_OUT_OF_LINE float crossfade(float a, float b, float p) { return s::crossfade(a, b, p); }
RACKPY_OUT_OF_LINE float_4 crossfade(float_4 a, float_4 b, float_4 p) { return s::crossfade(a, b, p); }

// sse_mathfun.h and sse_mathfun_extension.h.
RACKPY_OUT_OF_LINE float_4 sseOne() { return float_4(sse_mathfun_one_ps()); }
RACKPY_OUT_OF_LINE float_4 sseLog(float_4 x) { return float_4(sse_mathfun_log_ps(x.v)); }
RACKPY_OUT_OF_LINE float_4 sseExp(float_4 x) { return float_4(sse_mathfun_exp_ps(x.v)); }
RACKPY_OUT_OF_LINE float_4 sseSin(float_4 x) { return float_4(sse_mathfun_sin_ps(x.v)); }
RACKPY_OUT_OF_LINE float_4 sseCos(float_4 x) { return float_4(sse_mathfun_cos_ps(x.v)); }
RACKPY_OUT_OF_LINE void sseSincos(float_4 x, float_4* s, float_4* c) {
	__m128 sv, cv;
	sse_mathfun_sincos_ps(x.v, &sv, &cv);
	*s = float_4(sv);
	*c = float_4(cv);
}
RACKPY_OUT_OF_LINE float_4 sseTancot(float_4 x, int cotFlag) { return float_4(sse_mathfun_tancot_ps(x.v, cotFlag)); }
RACKPY_OUT_OF_LINE float_4 sseTan(float_4 x) { return float_4(sse_mathfun_tan_ps(x.v)); }
RACKPY_OUT_OF_LINE float_4 sseCot(float_4 x) { return float_4(sse_mathfun_cot_ps(x.v)); }
RACKPY_OUT_OF_LINE float_4 sseAtan(float_4 x) { return float_4(sse_mathfun_atan_ps(x.v)); }
RACKPY_OUT_OF_LINE float_4 sseAtan2(float_4 y, float_4 x) { return float_4(sse_mathfun_atan2_ps(y.v, x.v)); }

}  // namespace simd_calls
}  // namespace rackpy
