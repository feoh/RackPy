// SPDX-License-Identifier: GPL-3.0-or-later
// Family 6 DSP bindings (RP-18): rack::dsp -> rackpy.raw.dsp. dsp.cpp (the
// family adapter) calls them from bind_dsp(), after the SIMD types they use.
// The kernel runtime's Python API (kernel.hpp, RP-17) is separate: it is not a
// Rack binding and lives in the host module.
#pragma once
#include <pybind11/pybind11.h>

#include <rack.hpp>

namespace rackpy {
namespace dsp_bindings {

namespace py = pybind11;

/** dsp/common.hpp, approx.hpp, convert.hpp, window.hpp and ode.hpp (RP-18A). */
void bindBasic(py::module_ dsp);
/** dsp/digital.hpp: triggers, pulse generator, timers and clock divider (RP-18B). */
void bindDigital(py::module_ dsp);
/** dsp/filter.hpp: RC, exponential, peak and slew filters, IIRFilter and TBiquadFilter (RP-18C). */
void bindFilter(py::module_ dsp);
/** dsp/vumeter.hpp and dsp/midi.hpp: VU meters, MidiGenerator and MidiParser (RP-18D). */
void bindMidi(py::module_ dsp);
/** dsp/fft.hpp and dsp/fir.hpp (RP-19A). */
void bindAdvanced(py::module_ dsp);
/** dsp/minblep.hpp and dsp/resampler.hpp (RP-19B). */
void bindResampling(py::module_ dsp);
/** dsp/ringbuffer.hpp (RP-19C). */
void bindRingBuffers(py::module_ dsp);

/** The largest polynomial (coefficient count N) polyDirect/Horner/Estrin<T, N> are instantiated for. */
constexpr size_t MAX_POLY_COEFFICIENTS = 32;
/** The largest system the ODE steppers accept: Rack's steppers put len values per stage on the stack. */
constexpr int MAX_ODE_LEN = 4096;
/** IIRFilter<B_ORDER, A_ORDER, T> is instantiated for both orders from 2 to this (below 2, Rack's
process() writes past a zero-length array). */
constexpr int MAX_IIR_ORDER = 8;
/** The longest RealFFT and ComplexFFT RackPy makes: PFFFT puts 4 (real) or 8 (complex) bytes per point
of scratch on the stack, as Rack passes it no work buffer. */
constexpr long long MAX_FFT_LENGTH = 65536;
/** The largest RealTimeConvolver block (its FFTs have twice as many points). */
constexpr long long MAX_CONVOLVER_BLOCK = 32768;
/** The longest RealTimeConvolver kernel, in samples (its FFTs are allocated with unchecked mallocs). */
constexpr long long MAX_CONVOLVER_KERNEL = 1LL << 22;
/** The highest rate SampleRateConverter takes (speexdsp sizes its filter from the rate ratio). */
constexpr int MAX_SAMPLE_RATE = 768000 * 4;

}  // namespace dsp_bindings
}  // namespace rackpy
