// SPDX-License-Identifier: GPL-3.0-or-later
// Out-of-line calls to Rack's rack::dsp functions, shared by the bindings
// (basic.cpp) and the C++ reference (native/tests/dsp_reference.cpp), for the
// reason native/bindings/simd/calls.hpp gives: under the SDK's
// -funsafe-math-optimizations, Rack's inline code can round differently at
// each call site (polyDirect and polyEstrin by several ULPs where terms cancel,
// once -inf against NaN). Each wrapper is one non-inlined, non-IPA function per
// Rack function and instantiation, so the reference and the bindings run the
// same machine code.
#pragma once
#include <rack.hpp>

#include <algorithm>
#include <cstddef>

#ifndef RACKPY_OUT_OF_LINE
#define RACKPY_OUT_OF_LINE [[gnu::noipa]] inline
#endif

namespace rackpy {
namespace dsp_calls {

namespace d = rack::dsp;
using rack::simd::float_4;

// --- common.hpp ---------------------------------------------------------------------
RACKPY_OUT_OF_LINE float sinc(float x) { return d::sinc(x); }
RACKPY_OUT_OF_LINE float_4 sinc(float_4 x) { return d::sinc<float_4>(x); }
template <typename T>
RACKPY_OUT_OF_LINE T amplitudeToDb(T x) { return d::amplitudeToDb<T>(x); }
template <typename T>
RACKPY_OUT_OF_LINE T dbToAmplitude(T x) { return d::dbToAmplitude<T>(x); }
template <typename T>
RACKPY_OUT_OF_LINE T quadraticBipolar(T x) { return d::quadraticBipolar<T>(x); }
template <typename T>
RACKPY_OUT_OF_LINE T cubic(T x) { return d::cubic<T>(x); }
template <typename T>
RACKPY_OUT_OF_LINE T quarticBipolar(T x) { return d::quarticBipolar<T>(x); }
template <typename T>
RACKPY_OUT_OF_LINE T quintic(T x) { return d::quintic<T>(x); }
template <typename T>
RACKPY_OUT_OF_LINE T sqrtBipolar(T x) { return d::sqrtBipolar<T>(x); }
template <typename T>
RACKPY_OUT_OF_LINE T exponentialBipolar(T b, T x) { return d::exponentialBipolar<T>(b, x); }

// --- approx.hpp ---------------------------------------------------------------------
enum class Poly { Direct, Horner, Estrin };

template <Poly P, typename T, size_t N>
RACKPY_OUT_OF_LINE T polyN(const T* coefficients, T x) {
	T a[N];
	std::copy(coefficients, coefficients + N, a);
	if constexpr (P == Poly::Direct)
		return d::polyDirect(a, x);
	else if constexpr (P == Poly::Horner)
		return d::polyHorner(a, x);
	else
		return d::polyEstrin(a, x);
}

template <typename T>
RACKPY_OUT_OF_LINE T exp2Floor(T x, T* xf) { return d::exp2Floor(x, xf); }
template <typename T>
RACKPY_OUT_OF_LINE T approxExp2Floor(T x, T* xf) { return d::approxExp2Floor(x, xf); }
template <typename T>
RACKPY_OUT_OF_LINE T exp2Taylor5(T x) { return d::exp2_taylor5<T>(x); }
template <typename T>
RACKPY_OUT_OF_LINE T approxExp2Taylor5(T x) { return d::approxExp2_taylor5<T>(x); }

// --- convert.hpp --------------------------------------------------------------------
template <typename To, typename From>
RACKPY_OUT_OF_LINE To convert(From x) { return d::convert<To, From>(x); }
template <typename To, typename From>
RACKPY_OUT_OF_LINE void convertBuffer(const From* in, To* out, size_t len) { d::convert<To, From>(in, out, len); }

// --- window.hpp ---------------------------------------------------------------------
template <typename T>
RACKPY_OUT_OF_LINE T hann(T p) { return d::hann<T>(p); }
template <typename T>
RACKPY_OUT_OF_LINE T blackman(T alpha, T p) { return d::blackman<T>(alpha, p); }
template <typename T>
RACKPY_OUT_OF_LINE T blackmanNuttall(T p) { return d::blackmanNuttall<T>(p); }
template <typename T>
RACKPY_OUT_OF_LINE T blackmanHarris(T p) { return d::blackmanHarris<T>(p); }
RACKPY_OUT_OF_LINE void hannWindow(float* x, int len) { d::hannWindow(x, len); }
RACKPY_OUT_OF_LINE void blackmanWindow(float alpha, float* x, int len) { d::blackmanWindow(alpha, x, len); }
RACKPY_OUT_OF_LINE void blackmanNuttallWindow(float* x, int len) { d::blackmanNuttallWindow(x, len); }
RACKPY_OUT_OF_LINE void blackmanHarrisWindow(float* x, int len) { d::blackmanHarrisWindow(x, len); }

// --- ode.hpp ------------------------------------------------------------------------
/** The system f(t, x, dxdt) as a function pointer and its context, so that Rack's steppers are
instantiated once, with one functor type, for the bindings and the reference alike. It may throw. */
template <typename T>
using OdeSystem = void (*)(void* context, T t, const T* x, T* dxdt);

enum class Method { Euler, RK2, RK4 };

template <Method M, typename T>
RACKPY_OUT_OF_LINE void step(T t, T dt, T* x, int len, OdeSystem<T> f, void* context) {
	auto system = [f, context](T ti, const T* xi, T* dxdt) { f(context, ti, xi, dxdt); };
	if constexpr (M == Method::Euler)
		d::stepEuler(t, dt, x, len, system);
	else if constexpr (M == Method::RK2)
		d::stepRK2(t, dt, x, len, system);
	else
		d::stepRK4(t, dt, x, len, system);
}

// --- Members of Rack's stateful DSP objects (filter.hpp) -----------------------------
/** obj.*M(args...), out of line, one function per member and argument types:
`member<&TRCFilter<float>::process>(filter, x)`. */
template <auto M, typename Object, typename... Args>
RACKPY_OUT_OF_LINE decltype(auto) member(Object& object, Args... args) {
	return (object.*M)(args...);
}

}  // namespace dsp_calls
}  // namespace rackpy
