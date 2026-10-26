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

/** The largest polynomial (coefficient count N) polyDirect/Horner/Estrin<T, N> are instantiated for. */
constexpr size_t MAX_POLY_COEFFICIENTS = 32;
/** The largest system the ODE steppers accept: Rack's steppers put len values per stage on the stack. */
constexpr int MAX_ODE_LEN = 4096;

}  // namespace dsp_bindings
}  // namespace rackpy
