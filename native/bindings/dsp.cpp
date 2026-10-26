// SPDX-License-Identifier: GPL-3.0-or-later
// Family 6 adapter (RP-20, RP-18, RP-19): the generated registry calls bind_dsp().
// SIMD is in native/bindings/simd/. The kernel runtime's Python API
// (rackpy.dsp.kernel, RP-17) is not a Rack binding; the host module binds it.
#include "simd/simd.hpp"

namespace rackpy {
namespace bindings {

void bind_dsp(pybind11::module_ raw) {
	pybind11::module_ simd = raw.def_submodule("simd", "rack::simd: Rack's 4-lane SSE vectors and the math on them.");
	simd_bindings::bindVectors(simd);
	simd_bindings::bindFunctions(simd);
	simd_bindings::bindSseMathfun(raw);
}

}  // namespace bindings
}  // namespace rackpy
