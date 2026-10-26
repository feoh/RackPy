// SPDX-License-Identifier: GPL-3.0-or-later
// Family 6 adapter (RP-20, RP-18, RP-19): the generated registry calls bind_dsp().
// SIMD is in native/bindings/simd/, the DSP algorithms in native/bindings/dsp/.
// The kernel runtime's Python API (rackpy.dsp.kernel, RP-17) is not a Rack
// binding; the host module binds it.
#include "dsp/dsp.hpp"
#include "simd/simd.hpp"

namespace rackpy {
namespace bindings {

void bind_dsp(pybind11::module_ raw) {
	pybind11::module_ simd = raw.def_submodule("simd", "rack::simd: Rack's 4-lane SSE vectors and the math on them.");
	simd_bindings::bindVectors(simd);
	simd_bindings::bindFunctions(simd);
	simd_bindings::bindSseMathfun(raw);
	pybind11::module_ dsp = raw.def_submodule("dsp", "rack::dsp: Rack's DSP helpers, triggers, filters and MIDI processors.");
	dsp_bindings::bindBasic(dsp);
	dsp_bindings::bindDigital(dsp);
	dsp_bindings::bindFilter(dsp);
	dsp_bindings::bindMidi(dsp);
	dsp_bindings::bindAdvanced(dsp);
	dsp_bindings::bindResampling(dsp);
	dsp_bindings::bindRingBuffers(dsp);
}

}  // namespace bindings
}  // namespace rackpy
