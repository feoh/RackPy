// SPDX-License-Identifier: GPL-3.0-or-later
// dsp/ringbuffer.hpp -> rackpy.raw.dsp (RP-19C): RingBuffer<T, S>, and the
// registration of all three families (ringbuffer.hpp has the contract).
//
// RingBuffer is Rack's single-producer, single-consumer lock-free queue. Its
// indices only grow and are taken modulo S, so pushing to a full buffer
// overwrites the oldest elements and shifting an empty one returns stale ones,
// as in Rack (size() then wraps); RackPy refuses only the bulk copies past S,
// which would write past the data.
#include "ringbuffer.hpp"

#include <memory>
#include <utility>

namespace rackpy {
namespace dsp_bindings {
namespace ring {
namespace {

namespace c = dsp_calls;

template <typename T, size_t S>
void bindRing(py::module_ scope, py::dict& specs) {
	using R = d::RingBuffer<T, S>;
	using E = Element<T>;
	std::string name = className<R>("RingBuffer", E::name(), {S});
	py::class_<R> cls(scope, name.c_str(), ("rack::dsp::RingBuffer<" + E::name() + ", " + std::to_string(S) +
		">: a single-producer, single-consumer lock-free queue of " + std::to_string(S) + " elements.").c_str());
	cls.attr("S") = S;
	auto limit = [](size_t n, const char* what) {
		if (n > S)
			throw py::value_error(std::string(what) + ": n must be at most S (" + std::to_string(S) +
				"); Rack would copy past its data");
	};
	cls.def(py::init([] { return std::unique_ptr<R>(c::make<R>()); }), "Empty, with zeroed data.")
		.def_property("start", [](const R& r) { return r.start.load(); }, [](R& r, size_t v) { r.start = v; },
			"std::atomic<size_t>; Rack takes it modulo S.")
		.def_property("end", [](const R& r) { return r.end.load(); }, [](R& r, size_t v) { r.end = v; },
			"std::atomic<size_t>; Rack takes it modulo S.")
		.def_property_readonly("data", [](py::object self) { return elements<T>(self, self.cast<R&>().data, S, false); },
			"T data[S] (a live view; Frame elements as S * CHANNELS floats).")
		.def("push", [](R& r, py::handle t) { c::member<&R::push>(r, E::from(t)); }, py::arg("t"),
			"Appends t; when full, overwrites the oldest element, as in Rack.")
		.def("push_buffer", [limit](R& r, const py::buffer& t, int n) {
			if (n < 0)
				throw py::value_error("push_buffer: n must not be negative");
			limit((size_t) n, "push_buffer");
			const float* in = lanes<T>(t, false, (size_t) n, "push_buffer");
			std::vector<T> tmp = scratch<T>((size_t) n);
			std::memcpy(tmp.data(), in, sizeof(T) * (size_t) n);
			const T* p = tmp.data();
			c::member<&R::pushBuffer>(r, p, n);
		}, py::arg("t"), py::arg("n"), "Appends n elements from a float32 buffer of their lanes; n at most S.")
		.def("shift", [](R& r) { return E::to(c::member<&R::shift>(r)); },
			"Removes and returns the first element; when empty, a stale one, as in Rack.")
		.def("shift_buffer", [limit](R& r, const py::buffer& t, size_t n) {
			limit(n, "shift_buffer");
			float* out = lanes<T>(t, true, n, "shift_buffer");
			std::vector<T> tmp = scratch<T>(n);
			T* p = tmp.data();
			c::member<&R::shiftBuffer>(r, p, n);
			std::memcpy(out, tmp.data(), sizeof(T) * n);
		}, py::arg("t"), py::arg("n"), "Removes n elements into a writable float32 buffer of their lanes; n at most S.")
		.def("clear", [](R& r) { c::member<&R::clear>(r); })
		.def("empty", [](const R& r) { return c::member<&R::empty>(r); })
		.def("full", [](const R& r) { return c::member<&R::full>(r); })
		.def("size", [](const R& r) { return c::member<&R::size>(r); }, "end - start (wraps past an underrun, as in Rack).")
		.def("capacity", [](const R& r) { return c::member<&R::capacity>(r); }, "S - size() (wraps past an overrun, as in Rack).");
	specs[py::make_tuple(E::key(), S)] = cls;
}

template <typename T, size_t... K>
void sizes(py::module_ scope, py::dict& specs, std::index_sequence<K...>) {
	(bindRing<T, (size_t(16) << K)>(scope, specs), ...);
}

}  // namespace
}  // namespace ring

void bindRingBuffers(py::module_ m) {
	using namespace ring;
	py::module_ scope = m.def_submodule("_ringbuffers", "Instantiations of RingBuffer, DoubleRingBuffer and AppleRingBuffer; "
		"reach them by subscripting the templates.");
	py::dict rings, doubles, apples;
	auto twelve = std::make_index_sequence<12>();  // S = 16 to 32768
	sizes<float>(scope, rings, twelve);
	sizes<float_4>(scope, rings, twelve);
	sizes<d::Frame<2>>(scope, rings, twelve);
	sizes<d::Frame<8>>(scope, rings, twelve);
	sizes<d::Frame<16>>(scope, rings, twelve);
	bindDouble(scope, doubles);
	bindApple(scope, apples);
	simd_bindings::addTemplate(m, "RingBuffer", rings);
	simd_bindings::addTemplate(m, "DoubleRingBuffer", doubles);
	simd_bindings::addTemplate(m, "AppleRingBuffer", apples);
	// rackpy.raw._dsp re-keys the Frame instantiations once it has written Frame.
	py::dict all;
	all["RingBuffer"] = rings;
	all["DoubleRingBuffer"] = doubles;
	all["AppleRingBuffer"] = apples;
	scope.attr("SPECIALIZATIONS") = all;
	// Representative instantiations, named for the manifest and for typing.
	m.attr("RingBuffer_float_1024") = scope.attr("RingBuffer_float_1024");
	m.attr("DoubleRingBuffer_float_1024") = scope.attr("DoubleRingBuffer_float_1024");
	m.attr("AppleRingBuffer_float_64_1024") = scope.attr("AppleRingBuffer_float_64_1024");
}

}  // namespace dsp_bindings
}  // namespace rackpy
