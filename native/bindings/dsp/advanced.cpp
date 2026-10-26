// SPDX-License-Identifier: GPL-3.0-or-later
// dsp/fft.hpp and dsp/fir.hpp -> rackpy.raw.dsp (RP-19A): Rack's PFFFT wrappers,
// FIR helpers and real-time convolver.
//
// Rack's FFT objects own a PFFFT setup. PFFFT checks most of its size rules
// only with assert (a no-op in a release build) and returns a null setup for a
// length it cannot factor, which Rack's destructor then dereferences; it
// needs 16-byte-aligned buffers (SSE loads); and, given no work buffer, as Rack
// always does, it puts its scratch (4 bytes per point for a real transform, 8
// for a complex one) on the stack. So RackPy validates the length before Rack
// sees it, bounds it, and checks each buffer's item type, size and alignment.
// Every Rack call goes through calls.hpp's out-of-line wrappers, as the C++
// reference's do.
#include "dsp.hpp"

#include <pybind11/stl.h>

#include <cstdint>
#include <memory>
#include <string>

#include "calls.hpp"
#include "../values/values.hpp"

namespace rackpy {
namespace dsp_bindings {

namespace d = rack::dsp;
namespace c = dsp_calls;

namespace {

/** PFFFT's lengths: N = 2^a 3^b 5^c, a multiple of `multiple` (32 for real transforms, 16 for complex). */
bool pffftLength(long long n, int multiple) {
	if (n <= 0 || n % multiple != 0)
		return false;
	for (int p : {2, 3, 5})
		while (n % p == 0)
			n /= p;
	return n == 1;
}

size_t fftLength(const py::int_& value, int multiple, const char* what) {
	long long n = values::toInt<long long>(value, what, "size_t");
	if (!pffftLength(n, multiple))
		throw py::value_error(std::string(what) + ": PFFFT needs a length of the form 2^a 3^b 5^c that is a multiple of " +
			std::to_string(multiple) + ", not " + std::to_string(n));
	if (n > MAX_FFT_LENGTH)
		throw py::value_error(std::string(what) + ": RackPy allows lengths up to " + std::to_string(MAX_FFT_LENGTH) +
			" (PFFFT puts its scratch space on the stack)");
	return (size_t) n;
}

/** A contiguous 1-D float32 buffer of at least n items; `aligned` for PFFFT. */
float* floats(const py::buffer& buf, bool writable, size_t n, bool aligned, const char* what) {
	py::buffer_info info = buf.request(writable);
	if (!info.item_type_is_equivalent_to<float>())
		throw py::type_error(std::string(what) + " needs a buffer of float32 ('f') items, not '" + info.format + "'");
	if (info.ndim != 1 || info.strides[0] != (py::ssize_t) sizeof(float))
		throw py::value_error(std::string(what) + " needs a contiguous one-dimensional buffer");
	if ((size_t) info.size < n)
		throw py::value_error(std::string(what) + " needs at least " + std::to_string(n) + " floats; the buffer has " +
			std::to_string(info.size));
	if (aligned && reinterpret_cast<uintptr_t>(info.ptr) % 16 != 0)
		throw py::value_error(std::string(what) + ": PFFFT needs a 16-byte-aligned buffer (an array.array or numpy array "
			"is; a slice of one may not be)");
	return static_cast<float*>(info.ptr);
}

/** Input and output may be the same buffer (PFFFT allows aliasing), but not partly overlap. */
void sameOrApart(const float* in, size_t inLen, const float* out, size_t outLen, const char* what) {
	if (in == out)
		return;
	if (in < out + outLen && out < in + inLen)
		throw py::value_error(std::string(what) + ": the input and output overlap without being the same buffer");
}

/** The PFFFT_Setup* boundary type: an opaque handle with identity, which keeps its owner alive. */
struct SetupHandle {
	PFFFT_Setup* setup;
	py::object owner;
};

/** Rack's FFT with the length its setup was made for: the length field is writable and PFFFT_Setup
is opaque, so the true transform size is kept here for the buffer checks. */
template <typename F>
struct Sized : F {
	size_t points;
	explicit Sized(size_t n) : F(n), points(n) {}
	F& base() { return *this; }
};
using RealFFT = Sized<d::RealFFT>;
using ComplexFFT = Sized<d::ComplexFFT>;

template <typename F>
std::unique_ptr<F> makeFft(size_t n) {
	std::unique_ptr<F> fft(new F(n));
	if (!fft->setup) {
		// Not reachable for lengths that pass fftLength; Rack's destructor would dereference the null setup.
		(void) fft.release();
		throw py::value_error("PFFFT could not make a setup for this length");
	}
	return fft;
}

/** RealTimeConvolver's owning pointer fields: a view that finds the array again at each access, so it
never outlives a reallocation by set_kernel. */
struct ConvolverArray {
	py::object owner;
	int which;  // 0 kernelFfts, 1 inputFfts, 2 outputTail, 3 tmpBlock
	std::pair<float*, size_t> resolve() const {
		auto& v = owner.cast<d::RealTimeConvolver&>();
		switch (which) {
			case 0: return {v.kernelFfts, v.kernelFfts ? v.blockSize * 2 * v.kernelBlocks : 0};
			case 1: return {v.inputFfts, v.inputFfts ? v.blockSize * 2 * v.kernelBlocks : 0};
			case 2: return {v.outputTail, v.blockSize};
			default: return {v.tmpBlock, 2 * v.blockSize};
		}
	}
	float& at(int i) const {
		auto [data, n] = resolve();
		int size = (int) n;
		if (i < 0)
			i += size;
		if (i < 0 || i >= size)
			throw py::index_error("index out of range (the array has " + std::to_string(n) + " floats)");
		return data[i];
	}
};

size_t blockSize(const py::int_& value) {
	long long n = values::toInt<long long>(value, "RealTimeConvolver", "size_t");
	if (n < 16 || !pffftLength(2 * n, 32) || n > MAX_CONVOLVER_BLOCK)
		throw py::value_error("RealTimeConvolver: block_size must make 2 * block_size a PFFFT real length (block_size "
			"16, 32, 48, 64, ...; Rack recommends a power of 2 from 32) of at most " + std::to_string(MAX_CONVOLVER_BLOCK) +
			", not " + std::to_string(n));
	return (size_t) n;
}

}  // namespace

void bindAdvanced(py::module_ m) {
	py::class_<SetupHandle>(m, "PFFFT_Setup", "PFFFT's opaque setup (pffft.h): a handle with identity only, owned by "
		"the FFT object it came from, which it keeps alive.")
		.def("__bool__", [](const SetupHandle& h) { return h.setup != nullptr; })
		.def("__eq__", [](const SetupHandle& a, const SetupHandle& b) { return a.setup == b.setup; }, py::is_operator())
		.def("__hash__", [](const SetupHandle& h) { return std::hash<const void*>()(h.setup); })
		.def("__repr__", [](const SetupHandle&) { return std::string("<PFFFT_Setup>"); });

	// --- RealFFT ------------------------------------------------------------------
	py::class_<RealFFT> real(m, "RealFFT", "rack::dsp::RealFFT: PFFFT's real FFT of `length` points.");
	real.def(py::init([](const py::int_& length) { return makeFft<RealFFT>(fftLength(length, 32, "RealFFT")); }),
			py::arg("length"), "length: 2^a 3^b 5^c, a multiple of 32, at most 65536.")
		.def_property_readonly("setup", [](py::object self) { return SetupHandle{self.cast<RealFFT&>().setup, self}; },
			"The PFFFT setup (read-only: the FFT owns and frees it).")
		.def_property("length", [](const RealFFT& f) { return f.length; }, [](RealFFT& f, int n) { f.length = n; },
			"The length scale() uses; the transforms use the setup's.")
		.def("rfft_unordered", [](RealFFT& f, const py::buffer& input, const py::buffer& output) {
			size_t n = f.points;
			const float* in = floats(input, false, n, true, "rfft_unordered input");
			float* out = floats(output, true, n, true, "rfft_unordered output");
			sameOrApart(in, n, out, n, "rfft_unordered");
			c::member<&d::RealFFT::rfftUnordered>(f.base(), in, out);
		}, py::arg("input"), py::arg("output"), "Forward transform in PFFFT's internal order; length floats in and out.")
		.def("irfft_unordered", [](RealFFT& f, const py::buffer& input, const py::buffer& output) {
			size_t n = f.points;
			const float* in = floats(input, false, n, true, "irfft_unordered input");
			float* out = floats(output, true, n, true, "irfft_unordered output");
			sameOrApart(in, n, out, n, "irfft_unordered");
			c::member<&d::RealFFT::irfftUnordered>(f.base(), in, out);
		}, py::arg("input"), py::arg("output"), "Inverse of rfft_unordered, unscaled: irfft(rfft(x)) = length * x.")
		.def("rfft", [](RealFFT& f, const py::buffer& input, const py::buffer& output) {
			size_t n = f.points;
			const float* in = floats(input, false, n, true, "rfft input");
			float* out = floats(output, true, n, true, "rfft output");
			sameOrApart(in, n, out, n, "rfft");
			c::member<&d::RealFFT::rfft>(f.base(), in, out);
		}, py::arg("input"), py::arg("output"),
			"Forward transform in canonical order: F(0), F(n/2), re F(1), im F(1), ..., re F(n/2-1), im F(n/2-1).")
		.def("irfft", [](RealFFT& f, const py::buffer& input, const py::buffer& output) {
			size_t n = f.points;
			const float* in = floats(input, false, n, true, "irfft input");
			float* out = floats(output, true, n, true, "irfft output");
			sameOrApart(in, n, out, n, "irfft");
			c::member<&d::RealFFT::irfft>(f.base(), in, out);
		}, py::arg("input"), py::arg("output"), "Inverse of rfft, unscaled.")
		.def("scale", [](RealFFT& f, const py::buffer& x) {
			if (f.length < 0)
				throw py::value_error("scale: length is negative");
			c::member<&d::RealFFT::scale>(f.base(), floats(x, true, (size_t) f.length, false, "scale"));
		}, py::arg("x"), "Multiplies the first length floats by 1 / length, in place.");

	// --- ComplexFFT ---------------------------------------------------------------
	py::class_<ComplexFFT> complex(m, "ComplexFFT", "rack::dsp::ComplexFFT: PFFFT's complex FFT of `length` points.");
	complex.def(py::init([](const py::int_& length) { return makeFft<ComplexFFT>(fftLength(length, 16, "ComplexFFT")); }),
			py::arg("length"), "length: 2^a 3^b 5^c, a multiple of 16, at most 65536.")
		.def_property_readonly("setup", [](py::object self) { return SetupHandle{self.cast<ComplexFFT&>().setup, self}; },
			"The PFFFT setup (read-only: the FFT owns and frees it).")
		.def_property("length", [](const ComplexFFT& f) { return f.length; }, [](ComplexFFT& f, int n) { f.length = n; },
			"The length scale() uses; the transforms use the setup's.");
#define RACKPY_COMPLEX(pyname, method, doc) \
	complex.def(pyname, [](ComplexFFT& f, const py::buffer& input, const py::buffer& output) { \
		size_t n = 2 * f.points; \
		const float* in = floats(input, false, n, true, pyname " input"); \
		float* out = floats(output, true, n, true, pyname " output"); \
		sameOrApart(in, n, out, n, pyname); \
		c::member<&d::ComplexFFT::method>(f.base(), in, out); \
	}, py::arg("input"), py::arg("output"), doc);
	RACKPY_COMPLEX("fft_unordered", fftUnordered, "Forward transform in PFFFT's internal order; 2 * length floats in and out.")
	RACKPY_COMPLEX("ifft_unordered", ifftUnordered, "Inverse of fft_unordered, unscaled: ifft(fft(x)) = length * x.")
	RACKPY_COMPLEX("fft", fft, "Forward transform of interleaved complex numbers (re, im, re, im, ...).")
	RACKPY_COMPLEX("ifft", ifft, "Inverse of fft, unscaled.")
#undef RACKPY_COMPLEX
	complex.def("scale", [](ComplexFFT& f, const py::buffer& x) {
		if (f.length < 0)
			throw py::value_error("scale: length is negative");
		c::member<&d::ComplexFFT::scale>(f.base(), floats(x, true, 2 * (size_t) f.length, false, "scale"));
	}, py::arg("x"), "Multiplies the first 2 * length floats by 1 / length, in place.");

	// --- fir.hpp ------------------------------------------------------------------
	m.def("convolve_naive", [](const py::buffer& in, const py::buffer& kernel, int len) {
		if (len < 0)
			throw py::value_error("convolve_naive: len is negative");
		const float* x = floats(in, false, (size_t) len, false, "convolve_naive in_");
		const float* k = floats(kernel, false, (size_t) len, false, "convolve_naive kernel");
		return c::convolveNaive(x, k, len);
	}, py::arg("in_"), py::arg("kernel"), py::arg("len"), "sum(in_[len - 1 - i] * kernel[i]): one output of a direct convolution.");
	m.def("boxcar_lowpass_ir", [](const py::buffer& out, int len, float cutoff) {
		if (len < 0)
			throw py::value_error("boxcar_lowpass_ir: len is negative");
		c::boxcarLowpassIR(floats(out, true, (size_t) len, false, "boxcar_lowpass_ir"), len, cutoff);
	}, py::arg("out"), py::arg("len"), py::arg("cutoff") = 0.5f,
		"Writes len taps of a windowless sinc lowpass at cutoff (a ratio of the sample rate).");

	py::class_<ConvolverArray>(m, "ConvolverArray", "One of RealTimeConvolver's arrays, as a live view that finds the "
		"array again at each access (set_kernel reallocates); it keeps the convolver alive.")
		.def("__len__", [](const ConvolverArray& v) { return v.resolve().second; })
		.def("__getitem__", [](const ConvolverArray& v, int i) { return v.at(i); }, py::arg("i"))
		.def("__setitem__", [](const ConvolverArray& v, int i, float x) { v.at(i) = x; }, py::arg("i"), py::arg("x"))
		.def("tolist", [](const ConvolverArray& v) {
			auto [data, n] = v.resolve();
			return std::vector<float>(data, data + n);
		});

	using RTC = d::RealTimeConvolver;
	auto array = [](int which) {
		return [which](py::object self) { return ConvolverArray{self, which}; };
	};
	py::class_<RTC> conv(m, "RealTimeConvolver", "rack::dsp::RealTimeConvolver: partitioned FFT convolution, a block at a time.");
	conv.def(py::init([](const py::int_& block) { return std::unique_ptr<RTC>(new RTC(blockSize(block))); }), py::arg("block_size"),
			"block_size: Rack recommends a power of 2 from 32; 2 * block_size must be a PFFFT real length.")
		.def_property_readonly("kernel_ffts", array(0), "The kernel's block FFTs (empty until set_kernel).")
		.def_property_readonly("input_ffts", array(1), "The input history's block FFTs.")
		.def_property_readonly("output_tail", array(2), "The tail added to the next block.")
		.def_property_readonly("tmp_block", array(3), "Scratch, 2 * block_size floats.")
		.def_property_readonly("block_size", [](const RTC& v) { return v.blockSize; }, "Read-only: the arrays are sized by it.")
		.def_property_readonly("kernel_blocks", [](const RTC& v) { return v.kernelBlocks; }, "Read-only: set by set_kernel.")
		.def_property("input_pos", [](const RTC& v) { return v.inputPos; },
			[](RTC& v, const py::int_& pos) {
				long long p = values::toInt<long long>(pos, "input_pos", "size_t");
				if (p < 0)
					throw py::value_error("input_pos is a size_t: it cannot be negative");
				v.inputPos = (size_t) p;
			},
			"The newest input block's index; process_block reduces it modulo kernel_blocks.")
		.def_property_readonly("pffft", [](py::object self) { return SetupHandle{self.cast<RTC&>().pffft, self}; },
			"The PFFFT setup (read-only: the convolver owns and frees it).")
		.def("set_kernel", [](RTC& v, py::object kernel, const py::int_& length) {
			long long n = values::toInt<long long>(length, "set_kernel", "size_t");
			if (n < 0 || n > MAX_CONVOLVER_KERNEL)
				throw py::value_error("set_kernel: length must be 0 to " + std::to_string(MAX_CONVOLVER_KERNEL));
			const float* k = nullptr;
			if (!kernel.is_none())
				k = floats(kernel.cast<py::buffer>(), false, (size_t) n, false, "set_kernel kernel");
			c::member<&RTC::setKernel>(v, k, (size_t) n);
		}, py::arg("kernel"), py::arg("length"), "Sets the impulse response (None or length 0 clears it). Allocates.")
		.def("process_block", [](RTC& v, const py::buffer& input, const py::buffer& output) {
			const float* in = floats(input, false, v.blockSize, false, "process_block input");
			float* out = floats(output, true, v.blockSize, false, "process_block output");
			c::member<&RTC::processBlock>(v, in, out);
		}, py::arg("input"), py::arg("output"), "Convolves block_size floats; output may be input (it is read first).");
}

}  // namespace dsp_bindings
}  // namespace rackpy
