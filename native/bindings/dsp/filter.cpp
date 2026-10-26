// SPDX-License-Identifier: GPL-3.0-or-later
// dsp/filter.hpp -> rackpy.raw.dsp (RP-18C): Rack's RC, exponential, peak and
// slew filters, IIRFilter and TBiquadFilter, as stateful native Python objects.
//
// Each object holds Rack's C++ struct, and every method runs Rack's inline
// code through calls.hpp's out-of-line member wrapper (the C++ reference calls
// the same wrappers). Each template is instantiated for T = float, named by
// Rack's alias (RCFilter, BiquadFilter, ...), and T = simd::float_4 (named
// TRCFilter_float_4, ...); the template itself is subscriptable. IIRFilter's
// orders are compile-time integers: RackPy instantiates B_ORDER and A_ORDER
// from 2 to MAX_IIR_ORDER (below 2, Rack's process() writes past a zero-length
// array).
//
// _process_block is the facade's bulk path (rackpy.dsp.filters.process_block):
// it calls the same wrapper once per sample over float32 buffers, so its
// results equal the per-call results exactly. It is a convenience, not Rack API.
#include "dsp.hpp"

#include <pybind11/complex.h>
#include <pybind11/native_enum.h>
#include <pybind11/stl.h>

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../simd/simd.hpp"
#include "../support/overrides.hpp"
#include "../values/values.hpp"
#include "calls.hpp"

namespace rackpy {
namespace dsp_bindings {

namespace d = rack::dsp;
namespace c = dsp_calls;
using rack::simd::float_4;

namespace {

template <typename T>
constexpr const char* typeName() {
	return std::is_same<T, float>::value ? "float" : "float_4";
}

/** A live view of a fixed C++ array field: indexing reads and writes the object, which it keeps alive.
A float_4 item is itself a live view of its element. */
template <typename T>
struct ArrayView {
	T* data;
	size_t size;
	py::object owner;
	size_t index(int i) const {
		int n = (int) size;
		if (i < 0)
			i += n;
		if (i < 0 || i >= n)
			throw py::index_error("index out of range (the array has " + std::to_string(size) + " items)");
		return (size_t) i;
	}
};

template <typename T>
void bindArrayView(py::module_ m, const char* name) {
	py::class_<ArrayView<T>>(m, name, "A live view of a fixed array field of a Rack DSP object; it keeps the object alive.")
		.def("__len__", [](const ArrayView<T>& v) { return v.size; })
		.def("__getitem__", [](py::object self, int i) -> py::object {
			auto& v = self.cast<ArrayView<T>&>();
			T* item = &v.data[v.index(i)];
			if constexpr (std::is_same<T, float>::value)
				return py::float_(*item);
			else
				return py::cast(item, py::return_value_policy::reference_internal, self);
		}, py::arg("i"))
		.def("__setitem__", [](ArrayView<T>& v, int i, const T& x) { v.data[v.index(i)] = x; }, py::arg("i"), py::arg("x"))
		.def("tolist", [](const ArrayView<T>& v) {
			py::list out;
			for (size_t k = 0; k < v.size; k++)
				out.append(py::cast(T(v.data[k])));
			return out;
		}, "Copies of the items.")
		.def("__repr__", [name](const ArrayView<T>& v) {
			py::list out;
			for (size_t k = 0; k < v.size; k++)
				out.append(py::cast(T(v.data[k])));
			return std::string(name) + "(" + py::repr(out).cast<std::string>() + ")";
		});
}

template <typename T>
py::object view(py::object self, T* data, size_t n) {
	return py::cast(ArrayView<T>{data, n, std::move(self)});
}

template <typename Class>
void valueCopies(Class& cls) {
	using F = typename Class::type;
	cls.def("__copy__", [](const F& f) { return f; }).def("__deepcopy__", [](const F& f, py::dict) { return f; }, py::arg("memo"));
	cls.attr("__hash__") = py::none();
}

void deprecatedCall(const char* cls, const char* what, const char* instead) {
	bindings::deprecated((std::string(cls) + "." + what + " is deprecated in Rack; " + instead).c_str());
}

/** A float32 buffer of at least n items, read-only or writable. */
float* floats(const py::buffer& buf, bool writable, py::ssize_t n, const char* what) {
	py::buffer_info info = buf.request(writable);
	if (!info.item_type_is_equivalent_to<float>())
		throw py::type_error(std::string("process_block: ") + what + " needs a buffer of float32 ('f') items, not '" + info.format + "'");
	if (info.ndim != 1 || info.strides[0] != (py::ssize_t) sizeof(float))
		throw py::value_error(std::string("process_block: ") + what + " needs a contiguous one-dimensional buffer");
	if (info.size < n)
		throw py::value_error(std::string("process_block: ") + what + " is shorter than the input");
	return static_cast<float*>(info.ptr);
}

/** Buffers and length for the bulk path; in_ and out may be the same buffer (each sample is read before it is written). */
struct Block {
	py::buffer_info in;
	float* out;
	py::ssize_t n;
	Block(const py::buffer& input, const py::buffer& output) : in(input.request()) {
		if (!in.item_type_is_equivalent_to<float>() || in.ndim != 1 || in.strides[0] != (py::ssize_t) sizeof(float))
			throw py::type_error("process_block: in_ needs a contiguous one-dimensional buffer of float32 ('f') items");
		n = in.size;
		out = floats(output, true, n, "out");
	}
	const float* input() const { return static_cast<const float*>(in.ptr); }
};

// --- The single-pole filters ------------------------------------------------------

template <typename T>
py::class_<d::TRCFilter<T>> bindRC(py::module_ m, const char* name) {
	using F = d::TRCFilter<T>;
	py::class_<F> cls(m, name, (std::string("rack::dsp::TRCFilter<") + typeName<T>() + ">: an RC filter by Euler's method.").c_str());
	cls.def(py::init<>())
		.def_readwrite("c", &F::c)
		.def_property_readonly("xstate", [](py::object self) { return view(self, self.cast<F&>().xstate, 1); }, "T xstate[1] (a live view).")
		.def_property_readonly("ystate", [](py::object self) { return view(self, self.cast<F&>().ystate, 1); }, "T ystate[1] (a live view).")
		.def("reset", [](F& f) { c::member<&F::reset>(f); })
		.def("set_cutoff", [](F& f, const T& r) { c::member<&F::setCutoff>(f, r); }, py::arg("r"),
			"Sets the cutoff angular frequency in radians (c = 2 / r).")
		.def("set_cutoff_freq", [](F& f, const T& freq) { c::member<&F::setCutoffFreq>(f, freq); }, py::arg("f"),
			"Sets the cutoff as a ratio of the sample rate, f = f_c / f_s.")
		.def("process", [](F& f, const T& x) { c::member<&F::process>(f, x); }, py::arg("x"),
			"Steps the filter; read the result with lowpass() or highpass().")
		.def("lowpass", [](F& f) { return c::member<&F::lowpass>(f); })
		.def("highpass", [](F& f) { return c::member<&F::highpass>(f); });
	valueCopies(cls);
	return cls;
}

template <typename T>
py::class_<d::TExponentialFilter<T>> bindExponential(py::module_ m, const char* name, const char* shown) {
	using F = d::TExponentialFilter<T>;
	py::class_<F> cls(m, name, (std::string("rack::dsp::TExponentialFilter<") + typeName<T>() + ">: exponential smoothing.").c_str());
	cls.def(py::init<>())
		.def_readwrite("out", &F::out)
		.def_readwrite("lambda_", &F::lambda, "Rack's lambda (a Python keyword): the inverse time constant.")
		.def("reset", [](F& f) { c::member<&F::reset>(f); })
		.def("set_lambda", [](F& f, const T& l) { c::member<&F::setLambda>(f, l); }, py::arg("lambda_"))
		.def("set_tau", [](F& f, const T& tau) { c::member<&F::setTau>(f, tau); }, py::arg("tau"), "lambda = 1 / tau.")
		.def("process", [](F& f, const T& dt, const T& in) {
			return c::member<static_cast<T (F::*)(T, T)>(&F::process)>(f, dt, in);
		}, py::arg("delta_time"), py::arg("in_"), "Moves out toward in_ and returns it; snaps to in_ once a step no longer changes out.")
		.def("process", [shown](F& f, const T& in) {
			deprecatedCall(shown, "process(in_)", "use process(delta_time, in_)");
			RACKPY_CALL_DEPRECATED(return c::member<static_cast<T (F::*)(T)>(&F::process)>(f, in);)
		}, py::arg("in_"), "Deprecated in Rack: process(1, in_).");
	valueCopies(cls);
	return cls;
}

template <typename T>
py::class_<d::TPeakFilter<T>> bindPeak(py::module_ m, const char* name, const char* shown) {
	using F = d::TPeakFilter<T>;
	py::class_<F> cls(m, name, (std::string("rack::dsp::TPeakFilter<") + typeName<T>() + ">: jumps up, decays exponentially.").c_str());
	cls.def(py::init<>())
		.def_readwrite("out", &F::out)
		.def_readwrite("lambda_", &F::lambda, "Rack's lambda (a Python keyword): the decay's inverse time constant.")
		.def("reset", [](F& f) { c::member<&F::reset>(f); })
		.def("set_lambda", [](F& f, const T& l) { c::member<&F::setLambda>(f, l); }, py::arg("lambda_"))
		.def("set_tau", [](F& f, const T& tau) { c::member<&F::setTau>(f, tau); }, py::arg("tau"), "lambda = 1 / tau.")
		.def("process", [](F& f, const T& dt, const T& in) {
			return c::member<static_cast<T (F::*)(T, T)>(&F::process)>(f, dt, in);
		}, py::arg("delta_time"), py::arg("in_"), "Decays out toward in_, but never below it; returns out.")
		.def("process", [shown](F& f, const T& x) {
			deprecatedCall(shown, "process(x)", "use process(delta_time, in_)");
			RACKPY_CALL_DEPRECATED(return c::member<static_cast<T (F::*)(T)>(&F::process)>(f, x);)
		}, py::arg("x"), "Deprecated in Rack: process(1, x).")
		.def("peak", [shown](F& f) {
			deprecatedCall(shown, "peak()", "use the return value of process()");
			RACKPY_CALL_DEPRECATED(return c::member<&F::peak>(f);)
		}, "Deprecated in Rack: out.")
		.def("set_rate", [shown](F& f, const T& r) {
			deprecatedCall(shown, "set_rate()", "use set_lambda()");
			RACKPY_CALL_DEPRECATED(c::member<&F::setRate>(f, r);)
		}, py::arg("r"), "Deprecated in Rack: lambda = 1 - r.");
	valueCopies(cls);
	return cls;
}

template <typename T>
py::class_<d::TSlewLimiter<T>> bindSlew(py::module_ m, const char* name, const char* shown) {
	using F = d::TSlewLimiter<T>;
	py::class_<F> cls(m, name, (std::string("rack::dsp::TSlewLimiter<") + typeName<T>() + ">: limits the rate of change.").c_str());
	cls.def(py::init<>())
		.def_readwrite("out", &F::out)
		.def_readwrite("rise", &F::rise)
		.def_readwrite("fall", &F::fall)
		.def("reset", [](F& f) { c::member<&F::reset>(f); })
		.def("set_rise_fall", [](F& f, const T& rise, const T& fall) { c::member<&F::setRiseFall>(f, rise, fall); },
			py::arg("rise"), py::arg("fall"), "Rise and fall speeds in units per second.")
		.def("process", [](F& f, const T& dt, const T& in) {
			return c::member<static_cast<T (F::*)(T, T)>(&F::process)>(f, dt, in);
		}, py::arg("delta_time"), py::arg("in_"), "Moves out toward in_ by at most rise or fall times delta_time; returns it.")
		.def("process", [shown](F& f, const T& in) {
			deprecatedCall(shown, "process(in_)", "use process(delta_time, in_)");
			RACKPY_CALL_DEPRECATED(return c::member<static_cast<T (F::*)(T)>(&F::process)>(f, in);)
		}, py::arg("in_"), "Deprecated in Rack: process(1, in_).");
	valueCopies(cls);
	return cls;
}

template <typename T>
py::class_<d::TExponentialSlewLimiter<T>> bindExponentialSlew(py::module_ m, const char* name, const char* shown) {
	using F = d::TExponentialSlewLimiter<T>;
	py::class_<F> cls(m, name, (std::string("rack::dsp::TExponentialSlewLimiter<") + typeName<T>() +
		">: exponential smoothing with separate rise and fall rates.").c_str());
	cls.def(py::init<>())
		.def_readwrite("out", &F::out)
		.def_readwrite("rise_lambda", &F::riseLambda)
		.def_readwrite("fall_lambda", &F::fallLambda)
		.def("reset", [](F& f) { c::member<&F::reset>(f); })
		.def("set_rise_fall", [](F& f, const T& rise, const T& fall) { c::member<&F::setRiseFall>(f, rise, fall); },
			py::arg("rise_lambda"), py::arg("fall_lambda"))
		.def("set_rise_fall_tau", [](F& f, const T& rise, const T& fall) { c::member<&F::setRiseFallTau>(f, rise, fall); },
			py::arg("rise_tau"), py::arg("fall_tau"), "lambda = 1 / tau for each.")
		.def("process", [](F& f, const T& dt, const T& in) {
			return c::member<static_cast<T (F::*)(T, T)>(&F::process)>(f, dt, in);
		}, py::arg("delta_time"), py::arg("in_"), "Moves out toward in_ at the rise or fall rate; returns it.")
		.def("process", [shown](F& f, const T& in) {
			deprecatedCall(shown, "process(in_)", "use process(delta_time, in_)");
			RACKPY_CALL_DEPRECATED(return c::member<static_cast<T (F::*)(T)>(&F::process)>(f, in);)
		}, py::arg("in_"), "Deprecated in Rack: process(1, in_).");
	valueCopies(cls);
	return cls;
}

// --- IIRFilter and TBiquadFilter --------------------------------------------------

template <int B, int A, typename T>
std::string iirName() {
	return "IIRFilter_" + std::to_string(B) + "_" + std::to_string(A) + "_" + typeName<T>();
}

template <int B, int A, typename T>
py::class_<d::IIRFilter<B, A, T>> bindIIR(py::module_ scope, py::dict& specializations, py::module_ dsp) {
	using F = d::IIRFilter<B, A, T>;
	std::string name = iirName<B, A, T>();
	py::class_<F> cls(scope, name.c_str(), ("rack::dsp::IIRFilter<" + std::to_string(B) + ", " + std::to_string(A) + ", " +
		typeName<T>() + ">: an IIR filter with " + std::to_string(B) + " numerator and " + std::to_string(A) +
		" denominator coefficients (a_0 = 1 is omitted).").c_str());
	cls.attr("B_ORDER") = B;
	cls.attr("A_ORDER") = A;
	cls.def(py::init<>(), "Zero coefficients and state.")
		.def_property_readonly("b", [](py::object self) { return view(self, self.cast<F&>().b, B); }, "Numerator coefficients b_0.. (a live view).")
		.def_property_readonly("a", [](py::object self) { return view(self, self.cast<F&>().a, A - 1); },
			"Denominator coefficients a_1.. (a live view; a_0 = 1 is omitted).")
		.def_property_readonly("x", [](py::object self) { return view(self, self.cast<F&>().x, B - 1); }, "Input state x_{n-1}.. (a live view).")
		.def_property_readonly("y", [](py::object self) { return view(self, self.cast<F&>().y, A - 1); }, "Output state y_{n-1}.. (a live view).")
		.def("reset", [](F& f) { c::member<&F::reset>(f); }, "Zeroes the state, not the coefficients.")
		.def("set_coefficients", [](F& f, const std::vector<T>& b, const std::vector<T>& a) {
			if (b.size() != (size_t) B || a.size() != (size_t) (A - 1))
				throw py::value_error("set_coefficients needs " + std::to_string(B) + " b and " + std::to_string(A - 1) +
					" a coefficients (Rack reads exactly that many)");
			const T* bp = b.data();
			const T* ap = a.data();
			c::member<&F::setCoefficients>(f, bp, ap);
		}, py::arg("b"), py::arg("a"), "b: b_0 to b_{B-1}; a: a_1 to a_{A-1}. C++ takes two arrays.")
		.def("process", [](F& f, const T& in) { return c::member<&F::process>(f, in); }, py::arg("in_"))
		.def("get_transfer_function", [](F& f, const T& s) { return c::member<&F::getTransferFunction>(f, s); }, py::arg("s"),
			"H at the normalized angular frequency s = 2 pi f / f_s; a complex (a (real, imag) pair of float_4 for vectors).")
		.def("get_frequency_response", [](F& f, const T& freq) { return c::member<&F::getFrequencyResponse>(f, freq); },
			py::arg("f"), "|H| at f = f_c / f_s.")
		.def("get_frequency_phase", [](F& f, const T& freq) { return c::member<&F::getFrequencyPhase>(f, freq); },
			py::arg("f"), "arg H at f = f_c / f_s.");
	valueCopies(cls);
	py::object t = std::is_same<T, float>::value ? py::module_::import("builtins").attr("float") : py::object(py::type::of<float_4>());
	specializations[py::make_tuple(B, A, t)] = cls;
	if constexpr (std::is_same<T, float>::value)
		specializations[py::make_tuple(B, A)] = cls;  // T defaults to float
	if constexpr (B == 3 && A == 3)
		dsp.attr(name.c_str()) = cls;  // the biquads' base, named in rackpy.raw.dsp
	return cls;
}

template <typename T, int B, int... A>
void bindIIRRow(py::module_ scope, py::dict& specializations, py::module_ dsp, std::integer_sequence<int, A...>) {
	(bindIIR<B, A + 2, T>(scope, specializations, dsp), ...);
}

template <typename T, int... B>
void bindIIRs(py::module_ scope, py::dict& specializations, py::module_ dsp, std::integer_sequence<int, B...>) {
	(bindIIRRow<T, B + 2>(scope, specializations, dsp, std::make_integer_sequence<int, MAX_IIR_ORDER - 1>()), ...);
}

using BiquadType = d::TBiquadFilter<float>::Type;

/** TBiquadFilter<T>. Both instantiations take BiquadFilter.Type, the float one's enum: C++ has one
Type per instantiation, with the same values. */
template <typename T>
py::class_<d::TBiquadFilter<T>, d::IIRFilter<3, 3, T>> bindBiquad(py::module_ m, const char* name, py::object typeEnum) {
	using F = d::TBiquadFilter<T>;
	using Base = d::IIRFilter<3, 3, T>;
	py::class_<F, Base> cls(m, name, (std::string("rack::dsp::TBiquadFilter<") + typeName<T>() +
		">: a biquad (IIRFilter<3, 3>) with Rack's designs.").c_str());
	if (typeEnum.is_none()) {
		py::native_enum<BiquadType>(cls, "Type", "enum.Enum", "TBiquadFilter<>::Type")
			.value("LOWPASS_1POLE", BiquadType::LOWPASS_1POLE)
			.value("HIGHPASS_1POLE", BiquadType::HIGHPASS_1POLE)
			.value("LOWPASS", BiquadType::LOWPASS)
			.value("HIGHPASS", BiquadType::HIGHPASS)
			.value("LOWSHELF", BiquadType::LOWSHELF)
			.value("HIGHSHELF", BiquadType::HIGHSHELF)
			.value("BANDPASS", BiquadType::BANDPASS)
			.value("PEAK", BiquadType::PEAK)
			.value("NOTCH", BiquadType::NOTCH)
			.value("NUM_TYPES", BiquadType::NUM_TYPES)
			.finalize();
	}
	else {
		cls.attr("Type") = typeEnum;
	}
	cls.def(py::init<>(), "Rack sets LOWPASS at f = 0 and Q = 0, which divides 0 by 0: the coefficients start NaN.")
		.def("set_parameters", [](F& f, BiquadType type, float freq, float q, float v) {
			c::member<&F::setParameters>(f, static_cast<typename F::Type>(type), freq, q, v);
		}, py::arg("type"), py::arg("f"), py::arg("q"), py::arg("v"),
			"Sets the coefficients: f = f_c / f_s (below 0.5), q the quality factor, v the gain.");
	valueCopies(cls);
	return cls;
}

/** The bulk path for the float IIR filters (process(in)). */
template <typename F>
void addIIRBlock(py::module_ m) {
	m.def("_process_block", [](F& f, const py::buffer& input, const py::buffer& output) {
		Block block(input, output);
		const float* in = block.input();
		for (py::ssize_t k = 0; k < block.n; k++)
			block.out[k] = c::member<&F::process>(f, in[k]);
	}, py::arg("filter"), py::arg("in_"), py::arg("out"));
}

template <int B, int... A>
void addIIRBlockRow(py::module_ m, std::integer_sequence<int, A...>) {
	(addIIRBlock<d::IIRFilter<B, A + 2, float>>(m), ...);
}

template <int... B>
void addIIRBlocks(py::module_ m, std::integer_sequence<int, B...>) {
	(addIIRBlockRow<B + 2>(m, std::make_integer_sequence<int, MAX_IIR_ORDER - 1>()), ...);
}

/** The bulk path for the float filters with process(delta_time, in). */
template <typename F>
void addBlock(py::module_ m) {
	m.def("_process_block", [](F& f, const py::buffer& input, const py::buffer& output, float dt) {
		Block block(input, output);
		const float* in = block.input();
		for (py::ssize_t k = 0; k < block.n; k++)
			block.out[k] = c::member<static_cast<float (F::*)(float, float)>(&F::process)>(f, dt, in[k]);
	}, py::arg("filter"), py::arg("in_"), py::arg("out"), py::arg("delta_time"));
}

}  // namespace

void bindFilter(py::module_ m) {
	bindArrayView<float>(m, "FloatArray");
	bindArrayView<float_4>(m, "Float4Array");
	py::object f32 = py::module_::import("builtins").attr("float");
	py::object vector = py::type::of<float_4>();

	auto both = [&](const char* tmpl, py::object scalar, py::object vec) {
		py::dict specs;
		specs[py::make_tuple()] = scalar;
		specs[py::make_tuple(f32)] = scalar;
		specs[py::make_tuple(vector)] = vec;
		simd_bindings::addTemplate(m, tmpl, specs);
	};
	both("TRCFilter", bindRC<float>(m, "RCFilter"), bindRC<float_4>(m, "TRCFilter_float_4"));
	both("TExponentialFilter", bindExponential<float>(m, "ExponentialFilter", "ExponentialFilter"),
		bindExponential<float_4>(m, "TExponentialFilter_float_4", "TExponentialFilter_float_4"));
	both("TPeakFilter", bindPeak<float>(m, "PeakFilter", "PeakFilter"), bindPeak<float_4>(m, "TPeakFilter_float_4", "TPeakFilter_float_4"));
	both("TSlewLimiter", bindSlew<float>(m, "SlewLimiter", "SlewLimiter"), bindSlew<float_4>(m, "TSlewLimiter_float_4", "TSlewLimiter_float_4"));
	both("TExponentialSlewLimiter", bindExponentialSlew<float>(m, "ExponentialSlewLimiter", "ExponentialSlewLimiter"),
		bindExponentialSlew<float_4>(m, "TExponentialSlewLimiter_float_4", "TExponentialSlewLimiter_float_4"));

	addBlock<d::TExponentialFilter<float>>(m);
	addBlock<d::TPeakFilter<float>>(m);
	addBlock<d::TSlewLimiter<float>>(m);
	addBlock<d::TExponentialSlewLimiter<float>>(m);

	// IIRFilter<B, A, T>: every order pair from 2 to MAX_IIR_ORDER, for float and float_4, in a private scope;
	// IIRFilter[B, A, T] reaches each, and the (3, 3) ones are also named in rackpy.raw.dsp.
	py::module_ scope = m.def_submodule("_iir", "IIRFilter's instantiations; reach them as IIRFilter[B_ORDER, A_ORDER, T].");
	py::dict iirs;
	bindIIRs<float>(scope, iirs, m, std::make_integer_sequence<int, MAX_IIR_ORDER - 1>());
	bindIIRs<float_4>(scope, iirs, m, std::make_integer_sequence<int, MAX_IIR_ORDER - 1>());
	simd_bindings::addTemplate(m, "IIRFilter", iirs);

	addIIRBlocks(m, std::make_integer_sequence<int, MAX_IIR_ORDER - 1>());  // the float IIR filters' bulk path

	py::object biquad = bindBiquad<float>(m, "BiquadFilter", py::none());
	both("TBiquadFilter", biquad, bindBiquad<float_4>(m, "TBiquadFilter_float_4", biquad.attr("Type")));
}

}  // namespace dsp_bindings
}  // namespace rackpy
