// SPDX-License-Identifier: GPL-3.0-or-later
// dsp/minblep.hpp and dsp/resampler.hpp -> rackpy.raw.dsp (RP-19B): MinBLEP,
// and Rack's sample-rate converter, decimator and upsampler.
//
// The templates' integer parameters are compile-time sizes, so RackPy
// instantiates a bounded set of them (docs/reference/dsp.md#templates):
// MinBlepGenerator<Z, O, T> and Decimator<OVERSAMPLE, QUALITY, T> for powers of
// 2 and T = float or float_4, Upsampler<OVERSAMPLE, QUALITY> likewise, and
// SampleRateConverter<MAX_CHANNELS> for 1 to 16 channels. Each is reached by
// subscripting (MinBlepGenerator[16, 32]); the instantiations live in
// rackpy.raw.dsp._resampling, and the ones Rack's own modules use are also
// named in rackpy.raw.dsp.
//
// Rack's code indexes arrays with fields and arguments it does not check:
// RackPy keeps the index fields in range, refuses channel counts above
// MAX_CHANNELS (Rack only asserts) and makes SampleRateConverter's sizing
// fields read-only (speex indexes its state by channel without bounds
// checks). Every Rack call, constructors included, is out of line (calls.hpp).
#include "dsp.hpp"

#include <pybind11/stl.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../simd/simd.hpp"
#include "../values/values.hpp"
#include "arrays.hpp"
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

py::object typeObject(bool vector) {
	return vector ? py::object(py::type::of<float_4>()) : py::module_::import("builtins").attr("float");
}

/** A contiguous 1-D float32 buffer of at least n items (any alignment). */
float* floats(const py::buffer& buf, bool writable, size_t n, const char* what) {
	py::buffer_info info = buf.request(writable);
	if (!info.item_type_is_equivalent_to<float>())
		throw py::type_error(std::string(what) + " needs a buffer of float32 ('f') items, not '" + info.format + "'");
	if (info.ndim != 1 || info.strides[0] != (py::ssize_t) sizeof(float))
		throw py::value_error(std::string(what) + " needs a contiguous one-dimensional buffer");
	if ((size_t) info.size < n)
		throw py::value_error(std::string(what) + " needs at least " + std::to_string(n) + " floats; the buffer has " +
			std::to_string(info.size));
	return static_cast<float*>(info.ptr);
}

/** T values from Python: a float32 buffer for float, a list of float_4 for float_4. */
template <typename T>
std::vector<T> values(py::handle in, size_t n, const char* what) {
	std::vector<T> out(n);
	if constexpr (std::is_same<T, float>::value) {
		const float* p = floats(py::reinterpret_borrow<py::buffer>(in), false, n, what);
		std::copy(p, p + n, out.begin());
	}
	else {
		auto list = py::reinterpret_borrow<py::sequence>(in);
		if ((size_t) py::len(list) < n)
			throw py::value_error(std::string(what) + " needs at least " + std::to_string(n) + " vectors");
		for (size_t k = 0; k < n; k++)
			out[k] = list[k].cast<float_4>();
	}
	return out;
}

// --- minBlepImpulse and MinBlepGenerator --------------------------------------------

bool pffftRealLength(long long n) {
	if (n <= 0 || n % 32 != 0)
		return false;
	for (int p : {2, 3, 5})
		while (n % p == 0)
			n /= p;
	return n == 1;
}

template <int Z, int O, typename T>
py::object bindMinBlep(py::module_ scope, py::dict& specs) {
	using G = d::MinBlepGenerator<Z, O, T>;
	std::string name = "MinBlepGenerator_" + std::to_string(Z) + "_" + std::to_string(O) + "_" + typeName<T>();
	py::class_<G> cls(scope, name.c_str(), ("rack::dsp::MinBlepGenerator<" + std::to_string(Z) + ", " + std::to_string(O) + ", " +
		typeName<T>() + ">: band-limited steps, from a minimum-phase impulse of " + std::to_string(Z) +
		" zero crossings oversampled " + std::to_string(O) + " times.").c_str());
	cls.attr("Z") = Z;
	cls.attr("O") = O;
	cls.def(py::init([] { return std::unique_ptr<G>(c::make<G>()); }), "Computes the impulse (minBlepImpulse).")
		.def_property_readonly("buf", [](py::object self) { return view(self, self.cast<G&>().buf, 2 * Z); }, "The pending output (a live view).")
		.def_property("pos", [](const G& g) { return g.pos; }, [](G& g, int pos) {
			if (pos < 0 || pos >= 2 * Z)
				throw py::value_error("pos must be 0 to " + std::to_string(2 * Z - 1) + "; Rack indexes buf with it");
			g.pos = pos;
		})
		.def_property_readonly("impulse", [](py::object self) { return view(self, self.cast<G&>().impulse, 2 * Z * O + 1); },
			"The integrated impulse, 2 Z O + 1 floats ending at 1 (a live view).")
		.def("insert_discontinuity", [](G& g, float p, const T& x) { c::member<&G::insertDiscontinuity>(g, p, x); }, py::arg("p"),
			py::arg("x"), "Adds a step of x at p, -1 < p <= 0, relative to the current frame; other p are ignored, as in Rack.")
		.def("process", [](G& g) { return c::member<&G::process>(g); }, "The next output: the correction to add to the naive signal.");
	valueCopies(cls);
	specs[py::make_tuple(Z, O, typeObject(!std::is_same<T, float>::value))] = cls;
	if constexpr (std::is_same<T, float>::value)
		specs[py::make_tuple(Z, O)] = cls;
	return cls;
}

// --- SampleRateConverter ------------------------------------------------------------

struct StateHandle {
	SpeexResamplerState* st;
	py::object owner;
};

template <int N>
py::object bindConverter(py::module_ scope, py::dict& specs) {
	using S = d::SampleRateConverter<N>;
	std::string name = "SampleRateConverter_" + std::to_string(N);
	py::class_<S> cls(scope, name.c_str(), ("rack::dsp::SampleRateConverter<" + std::to_string(N) +
		">: speexdsp's resampler for up to " + std::to_string(N) + " interleaved channels.").c_str());
	cls.attr("MAX_CHANNELS") = N;
	auto rate = [](int r, const char* what) {
		if (r <= 0 || r > MAX_SAMPLE_RATE)
			throw py::value_error(std::string(what) + ": a rate must be 1 to " + std::to_string(MAX_SAMPLE_RATE));
		return r;
	};
	cls.def(py::init([] { return std::unique_ptr<S>(c::make<S>()); }), "MAX_CHANNELS channels, 44100 to 44100, quality 4.")
		.def_property_readonly("st", [](py::object self) { return StateHandle{self.cast<S&>().st, self}; },
			"speexdsp's state (an opaque handle, None-like when Rack copies instead; read-only: the converter owns it).")
		.def_property_readonly("channels", [](const S& s) { return s.channels; },
			"Read-only: speexdsp's state is sized by it, so set it with set_channels().")
		.def_property("quality", [](const S& s) { return s.quality; }, [](S& s, int q) { s.quality = q; },
			"0 to 10; assigning does not rebuild the state (set_quality() does).")
		.def_property("in_rate", [](const S& s) { return s.inRate; }, [rate](S& s, int r) { s.inRate = rate(r, "in_rate"); },
			"Assigning does not rebuild the state (set_rates() does).")
		.def_property("out_rate", [](const S& s) { return s.outRate; }, [rate](S& s, int r) { s.outRate = rate(r, "out_rate"); },
			"Assigning does not rebuild the state (set_rates() does).")
		.def("set_channels", [](S& s, int channels) {
			if (channels < 0 || channels > N)
				throw py::value_error("set_channels: channels must be 0 to " + std::to_string(N) + "; Rack only asserts it");
			c::member<&S::setChannels>(s, channels);
		}, py::arg("channels"))
		.def("set_quality", [](S& s, int quality) {
			if (quality < 0 || quality > 10)
				throw py::value_error("set_quality: quality must be 0 (fastest) to 10 (best)");
			c::member<&S::setQuality>(s, quality);
		}, py::arg("quality"))
		.def("set_rates", [rate](S& s, int in, int out) { c::member<&S::setRates>(s, rate(in, "set_rates"), rate(out, "set_rates")); },
			py::arg("in_rate"), py::arg("out_rate"))
		.def("refresh_state", [](S& s) { c::member<&S::refreshState>(s); }, "Rebuilds speexdsp's state from the fields.")
		.def("process", [](S& s, const py::buffer& in, int inStride, int inFrames, const py::buffer& out, int outStride, int outFrames) {
			if (inStride < 1 || outStride < 1 || inFrames < 0 || outFrames < 0)
				throw py::value_error("process: strides must be at least 1 and frame counts not negative");
			if (inStride < s.channels || outStride < s.channels)
				throw py::value_error("process: a stride must be at least channels (" + std::to_string(s.channels) + ")");
			size_t inNeed = inFrames ? (size_t) inStride * (inFrames - 1) + s.channels : 0;
			size_t outNeed = outFrames ? (size_t) outStride * (outFrames - 1) + s.channels : 0;
			const float* ip = floats(in, false, inNeed, "process in_");
			float* op = floats(out, true, outNeed, "process out");
			int inUsed = inFrames, outUsed = outFrames;
			c::member<static_cast<void (S::*)(const float*, int, int*, float*, int, int*)>(&S::process)>(s, ip, inStride, &inUsed, op,
				outStride, &outUsed);
			return py::make_tuple(inUsed, outUsed);
		}, py::arg("in_"), py::arg("in_stride"), py::arg("in_frames"), py::arg("out"), py::arg("out_stride"), py::arg("out_frames"),
			"Converts up to in_frames frames into up to out_frames; returns (frames read, frames written). Channel c of "
			"frame i is at i * stride + c. C++ returns the counts through its two int pointers.")
		.def("process", [](S& s, const py::list& in, int inFrames, const py::list& out, int outFrames) {
			// Frames are separate Python objects: gather them into Rack's contiguous Frame array and back.
			if (inFrames < 0 || outFrames < 0 || (size_t) inFrames > in.size() || (size_t) outFrames > out.size())
				throw py::value_error("process: the frame counts must be within the lists");
			py::object frameType = py::module_::import("rackpy.raw.dsp").attr("Frame")[py::make_tuple(N, py::module_::import("builtins").attr("float"))];
			std::vector<d::Frame<N>> src((size_t) inFrames), dst((size_t) outFrames);
			for (int k = 0; k < inFrames; k++) {
				py::object frame = in[(size_t) k];
				if (!py::isinstance(frame, frameType))
					throw py::type_error("process: the frames must be Frame[" + std::to_string(N) + "] objects");
				py::buffer_info info = py::buffer(frame).request();
				std::memcpy(src[(size_t) k].samples, info.ptr, sizeof(float) * N);
			}
			for (int k = 0; k < outFrames; k++)
				if (!py::isinstance(out[(size_t) k], frameType))
					throw py::type_error("process: the frames must be Frame[" + std::to_string(N) + "] objects");
			int inUsed = inFrames, outUsed = outFrames;
			c::member<static_cast<void (S::*)(const d::Frame<N>*, int*, d::Frame<N>*, int*)>(&S::process)>(s,
				(const d::Frame<N>*) src.data(), &inUsed, dst.data(), &outUsed);
			for (int k = 0; k < outUsed; k++) {
				py::buffer_info info = py::buffer(out[(size_t) k]).request(true);
				std::memcpy(info.ptr, dst[(size_t) k].samples, sizeof(float) * N);
			}
			return py::make_tuple(inUsed, outUsed);
		}, py::arg("in_"), py::arg("in_frames"), py::arg("out"), py::arg("out_frames"),
			"The same for lists of Frame[MAX_CHANNELS] objects (copied into and out of Rack's contiguous frames).");
	specs[py::make_tuple(N)] = cls;
	return cls;
}

// --- Decimator and Upsampler ----------------------------------------------------------

template <int OS, int Q, typename T>
py::object bindDecimator(py::module_ scope, py::dict& specs) {
	using D = d::Decimator<OS, Q, T>;
	std::string name = "Decimator_" + std::to_string(OS) + "_" + std::to_string(Q) + "_" + typeName<T>();
	py::class_<D> cls(scope, name.c_str(), ("rack::dsp::Decimator<" + std::to_string(OS) + ", " + std::to_string(Q) + ", " +
		typeName<T>() + ">: downsamples by " + std::to_string(OS) + " with a " + std::to_string(OS * Q) + "-tap filter.").c_str());
	cls.attr("OVERSAMPLE") = OS;
	cls.attr("QUALITY") = Q;
	cls.def(py::init([](float cutoff) { return std::unique_ptr<D>(c::make<D>(cutoff)); }), py::arg("cutoff") = 0.9f,
			"cutoff: the passband edge, a ratio of the output's Nyquist frequency.")
		.def_property_readonly("in_buffer", [](py::object self) { return view(self, self.cast<D&>().inBuffer, OS * Q); })
		.def_property_readonly("kernel", [](py::object self) { return view(self, self.cast<D&>().kernel, OS * Q); })
		.def_property("in_index", [](const D& v) { return v.inIndex; }, [](D& v, int i) {
			if (i < 0 || i > OS * Q - OS)
				throw py::value_error("in_index must be 0 to " + std::to_string(OS * Q - OS) + "; Rack copies OVERSAMPLE values from it");
			v.inIndex = i;
		})
		.def("reset", [](D& v) { c::member<&D::reset>(v); })
		.def("process", [](D& v, py::handle in) {
			std::vector<T> x = values<T>(in, OS, "process");
			T* p = x.data();
			return c::member<&D::process>(v, p);
		}, py::arg("in_"), "One output from OVERSAMPLE inputs: a float32 buffer, or a list of float_4 for vectors.");
	valueCopies(cls);
	specs[py::make_tuple(OS, Q, typeObject(!std::is_same<T, float>::value))] = cls;
	if constexpr (std::is_same<T, float>::value)
		specs[py::make_tuple(OS, Q)] = cls;
	return cls;
}

template <int OS, int Q>
py::object bindUpsampler(py::module_ scope, py::dict& specs) {
	using U = d::Upsampler<OS, Q>;
	std::string name = "Upsampler_" + std::to_string(OS) + "_" + std::to_string(Q);
	py::class_<U> cls(scope, name.c_str(), ("rack::dsp::Upsampler<" + std::to_string(OS) + ", " + std::to_string(Q) +
		">: upsamples by " + std::to_string(OS) + " with a " + std::to_string(OS * Q) + "-tap filter.").c_str());
	cls.attr("OVERSAMPLE") = OS;
	cls.attr("QUALITY") = Q;
	cls.def(py::init([](float cutoff) { return std::unique_ptr<U>(c::make<U>(cutoff)); }), py::arg("cutoff") = 0.9f)
		.def_property_readonly("in_buffer", [](py::object self) { return view(self, self.cast<U&>().inBuffer, Q); })
		.def_property_readonly("kernel", [](py::object self) { return view(self, self.cast<U&>().kernel, OS * Q); })
		.def_property("in_index", [](const U& v) { return v.inIndex; }, [](U& v, int i) {
			if (i < 0 || i >= Q)
				throw py::value_error("in_index must be 0 to " + std::to_string(Q - 1) + "; Rack indexes in_buffer with it");
			v.inIndex = i;
		})
		.def("reset", [](U& v) { c::member<&U::reset>(v); })
		.def("process", [](U& v, float in, const py::buffer& out) {
			c::member<&U::process>(v, in, floats(out, true, OS, "process out"));
		}, py::arg("in_"), py::arg("out"), "Writes OVERSAMPLE outputs for one input into a float32 buffer.");
	valueCopies(cls);
	specs[py::make_tuple(OS, Q)] = cls;
	return cls;
}

// The bounded sets (powers of 2): Z and O 4 to 64 for MinBLEP; OVERSAMPLE and QUALITY 2 to 32 for the others.
template <typename T, int Z, int... L>
void minBlepRow(py::module_ scope, py::dict& specs, std::integer_sequence<int, L...>) {
	(bindMinBlep<Z, (4 << L), T>(scope, specs), ...);
}
template <typename T, int... L>
void minBleps(py::module_ scope, py::dict& specs, std::integer_sequence<int, L...>) {
	(minBlepRow<T, (4 << L)>(scope, specs, std::make_integer_sequence<int, 5>()), ...);
}
template <typename T, int OS, int... L>
void decimatorRow(py::module_ scope, py::dict& specs, std::integer_sequence<int, L...>) {
	(bindDecimator<OS, (2 << L), T>(scope, specs), ...);
}
template <typename T, int... L>
void decimators(py::module_ scope, py::dict& specs, std::integer_sequence<int, L...>) {
	(decimatorRow<T, (2 << L)>(scope, specs, std::make_integer_sequence<int, 5>()), ...);
}
template <int OS, int... L>
void upsamplerRow(py::module_ scope, py::dict& specs, std::integer_sequence<int, L...>) {
	(bindUpsampler<OS, (2 << L)>(scope, specs), ...);
}
template <int... L>
void upsamplers(py::module_ scope, py::dict& specs, std::integer_sequence<int, L...>) {
	(upsamplerRow<(2 << L)>(scope, specs, std::make_integer_sequence<int, 5>()), ...);
}
template <int... I>
void converters(py::module_ scope, py::dict& specs, std::integer_sequence<int, I...>) {
	(bindConverter<I + 1>(scope, specs), ...);
}

}  // namespace

void bindResampling(py::module_ m) {
	m.def("min_blep_impulse", [](int z, int o, const py::buffer& output) {
		long long n = 2LL * z * o;
		if (z < 1 || o < 1 || !pffftRealLength(n) || n > MAX_FFT_LENGTH)
			throw py::value_error("min_blep_impulse: 2 * z * o must be a PFFFT real length (2^a 3^b 5^c, a multiple of 32) of at "
				"most " + std::to_string(MAX_FFT_LENGTH) + "; Rack's RealFFT of that size would crash otherwise");
		c::minBlepImpulse(z, o, floats(output, true, (size_t) n, "min_blep_impulse output"));
	}, py::arg("z"), py::arg("o"), py::arg("output"),
		"Writes the integrated minimum-phase band-limited impulse, 2 z o floats from 0 to 1.");

	py::class_<StateHandle>(m, "SpeexResamplerState", "speexdsp's opaque resampler state: identity and truth only.")
		.def("__bool__", [](const StateHandle& h) { return h.st != nullptr; })
		.def("__eq__", [](const StateHandle& a, const StateHandle& b) { return a.st == b.st; }, py::is_operator())
		.def("__hash__", [](const StateHandle& h) { return std::hash<const void*>()(h.st); })
		.def("__repr__", [](const StateHandle& h) { return std::string(h.st ? "<SpeexResamplerState>" : "<SpeexResamplerState: none>"); });

	py::module_ scope = m.def_submodule("_resampling", "Instantiations of MinBlepGenerator, Decimator, Upsampler and "
		"SampleRateConverter; reach them by subscripting the templates.");
	py::dict bleps, decs, ups, srcs;
	minBleps<float>(scope, bleps, std::make_integer_sequence<int, 5>());
	minBleps<float_4>(scope, bleps, std::make_integer_sequence<int, 5>());
	decimators<float>(scope, decs, std::make_integer_sequence<int, 5>());
	decimators<float_4>(scope, decs, std::make_integer_sequence<int, 5>());
	upsamplers(scope, ups, std::make_integer_sequence<int, 5>());
	converters(scope, srcs, std::make_integer_sequence<int, 16>());
	simd_bindings::addTemplate(m, "MinBlepGenerator", bleps);
	simd_bindings::addTemplate(m, "Decimator", decs);
	simd_bindings::addTemplate(m, "Upsampler", ups);
	simd_bindings::addTemplate(m, "SampleRateConverter", srcs);
	// The instantiations Rack's own code uses, named for the manifest and for typing.
	m.attr("MinBlepGenerator_16_32_float") = bleps[py::make_tuple(16, 32)];
	m.attr("SampleRateConverter_16") = srcs[py::make_tuple(16)];
	m.attr("Decimator_16_16_float") = decs[py::make_tuple(16, 16)];
	m.attr("Upsampler_16_16") = ups[py::make_tuple(16, 16)];
}

}  // namespace dsp_bindings
}  // namespace rackpy
