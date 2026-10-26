// SPDX-License-Identifier: GPL-3.0-or-later
// sse_mathfun.h and sse_mathfun_extension.h (global namespace, reached through
// simd/functions.hpp) -> rackpy.raw.sse_mathfun. Julien Pommier's SSE
// approximations, as modified for Rack (zlib licence); rack::simd's sin, cos,
// tan, atan, atan2, log and exp call them. Their __m128 arguments and results
// are float_4 in Python.
#include "simd.hpp"

#include "../values/values.hpp"

namespace rackpy {
namespace simd_bindings {

using rack::simd::float_4;

void bindSseMathfun(py::module_ raw) {
	py::module_ m = raw.def_submodule("sse_mathfun",
		"sse_mathfun.h and sse_mathfun_extension.h: the SSE approximations behind rack::simd's sin, cos, log, exp, ...");
	m.def("sse_mathfun_one_ps", [] { return float_4(sse_mathfun_one_ps()); }, "1.0 in every lane, made without a memory load.");
	m.def("sse_mathfun_log_ps", [](const float_4& x) { return float_4(sse_mathfun_log_ps(x.v)); }, py::arg("x"),
		"Natural logarithm; NaN for x <= 0, zero included.");
	m.def("sse_mathfun_exp_ps", [](const float_4& x) { return float_4(sse_mathfun_exp_ps(x.v)); }, py::arg("x"), "e**x; saturates at about 2.406e38, also returned for inf and NaN.");
	m.def("sse_mathfun_sin_ps", [](const float_4& x) { return float_4(sse_mathfun_sin_ps(x.v)); }, py::arg("x"), "Sine.");
	m.def("sse_mathfun_cos_ps", [](const float_4& x) { return float_4(sse_mathfun_cos_ps(x.v)); }, py::arg("x"), "Cosine.");
	m.def("sse_mathfun_sincos_ps", [](const float_4& x) {
		__m128 s, c;
		sse_mathfun_sincos_ps(x.v, &s, &c);
		return py::make_tuple(float_4(s), float_4(c));
	}, py::arg("x"), "(sine, cosine) together; C++ writes them through two pointers.");
	m.def("sse_mathfun_tancot_ps", [](const float_4& x, const py::int_& cot_flag) {
		return float_4(sse_mathfun_tancot_ps(x.v, values::toInt<int>(cot_flag, "sse_mathfun_tancot_ps", "int")));
	}, py::arg("x"), py::arg("cot_flag"), "The tangent, or the cotangent if cot_flag is nonzero.");
	m.def("sse_mathfun_tan_ps", [](const float_4& x) { return float_4(sse_mathfun_tan_ps(x.v)); }, py::arg("x"), "Tangent.");
	m.def("sse_mathfun_cot_ps", [](const float_4& x) { return float_4(sse_mathfun_cot_ps(x.v)); }, py::arg("x"), "Cotangent.");
	m.def("sse_mathfun_atan_ps", [](const float_4& x) { return float_4(sse_mathfun_atan_ps(x.v)); }, py::arg("x"), "Arctangent.");
	m.def("sse_mathfun_atan2_ps", [](const float_4& y, const float_4& x) { return float_4(sse_mathfun_atan2_ps(y.v, x.v)); },
		py::arg("y"), py::arg("x"), "The angle of the point (x, y), as std::atan2(y, x).");
}

}  // namespace simd_bindings
}  // namespace rackpy
