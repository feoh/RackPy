// SPDX-License-Identifier: GPL-3.0-or-later
// dsp/ringbuffer.hpp -> rackpy.raw.dsp (RP-19C): AppleRingBuffer<T, S, N>.
//
// A window of S elements sliding along a block of N, moved back by
// return_buffer(). Its plain size_t indices are used unreduced, so RackPy keeps
// 0 <= start <= end <= N: it refuses field writes outside that, push() with a
// full block (Rack would write past it), shift() when empty (start would pass
// end and the next return_buffer would move a wrapped size), end_data(n) for
// more than the block can take, end_incr past N and start_incr past end. Up to
// N - S elements past S are Rack's to allow (full() and capacity() then follow
// its arithmetic).
#include "ringbuffer.hpp"

#include <memory>
#include <utility>

namespace rackpy {
namespace dsp_bindings {
namespace ring {
namespace {

namespace c = dsp_calls;

template <typename T, size_t S, size_t N>
void bindOne(py::module_ scope, py::dict& specs) {
	using R = d::AppleRingBuffer<T, S, N>;
	using E = Element<T>;
	std::string name = className<R>("AppleRingBuffer", E::name(), {S, N});
	py::class_<R> cls(scope, name.c_str(), ("rack::dsp::AppleRingBuffer<" + E::name() + ", " + std::to_string(S) + ", " +
		std::to_string(N) + ">: " + std::to_string(S) + " linear elements sliding along a block of " + std::to_string(N) +
		". Not thread-safe.").c_str());
	cls.attr("S") = S;
	cls.attr("N") = N;
	cls.def(py::init([] { return std::unique_ptr<R>(c::make<R>()); }), "Empty, with zeroed data.")
		.def_property("start", [](const R& r) { return r.start; }, [](R& r, size_t v) {
			if (v > r.end)
				throw py::value_error("start must be at most end (" + std::to_string(r.end) + "); Rack's size() would wrap");
			r.start = v;
		})
		.def_property("end", [](const R& r) { return r.end; }, [](R& r, size_t v) {
			if (v < r.start || v > N)
				throw py::value_error("end must be start (" + std::to_string(r.start) + ") to N (" + std::to_string(N) +
					"); Rack indexes data with it");
			r.end = v;
		})
		.def_property_readonly("data", [](py::object self) { return elements<T>(self, self.cast<R&>().data, N, false); },
			"T data[N] (a live view; Frame elements as floats).")
		.def("return_buffer", [](R& r) { c::member<&R::returnBuffer>(r); }, "Moves the elements to the start of the block.")
		.def("push", [](R& r, py::handle t) {
			T x = E::from(t);
			if (r.end - r.start >= N)
				throw py::value_error("push: the block of N (" + std::to_string(N) + ") is full; Rack would write past it");
			c::member<&R::push>(r, x);
		}, py::arg("t"))
		.def("shift", [](R& r) {
			if (r.start == r.end)
				throw py::value_error("shift: the buffer is empty; Rack's start would pass end");
			return E::to(c::member<&R::shift>(r));
		})
		.def("empty", [](const R& r) { return c::member<&R::empty>(r); })
		.def("full", [](const R& r) { return c::member<&R::full>(r); }, "size() == S.")
		.def("size", [](const R& r) { return c::member<&R::size>(r); })
		.def("capacity", [](const R& r) { return c::member<&R::capacity>(r); }, "S - size() (wraps above S, as in Rack).")
		.def("end_data", [](py::object self, size_t n) {
			R& r = self.cast<R&>();
			if (n > N - (r.end - r.start))
				throw py::value_error("end_data: n must be at most N - size() (" + std::to_string(N - (r.end - r.start)) +
					"); Rack would give fewer");
			return elements<T>(self, c::member<&R::endData>(r, n), n, false);
		}, py::arg("n"), "n writable elements at the end, moving the window back first if needed (a view); then end_incr.")
		.def("end_incr", [](R& r, size_t n) {
			if (n > N - r.end)
				throw py::value_error("end_incr: end + n must be at most N (" + std::to_string(N) + "); call end_data(n) first");
			c::member<&R::endIncr>(r, n);
		}, py::arg("n"))
		.def("start_data", [](py::object self) {
			const R& r = self.cast<const R&>();
			return elements<T>(self, c::member<&R::startData>(r), r.end - r.start, true);
		}, "The size() elements from the start (a read-only view: C++ returns const T*).")
		.def("start_incr", [](R& r, size_t n) {
			if (n > r.end - r.start)
				throw py::value_error("start_incr: n must be at most size() (" + std::to_string(r.end - r.start) + ")");
			c::member<&R::startIncr>(r, n);
		}, py::arg("n"));
	valueCopies(cls);
	specs[py::make_tuple(E::key(), S, N)] = cls;
}

template <typename T, size_t... K>
void sizes(py::module_ scope, py::dict& specs, std::index_sequence<K...>) {
	(bindOne<T, (size_t(16) << K), (size_t(256) << K)>(scope, specs), ...);
}

}  // namespace

void bindApple(py::module_ scope, py::dict& specs) {
	auto nine = std::make_index_sequence<9>();  // S = 16 to 4096, N = 16 S
	sizes<float>(scope, specs, nine);
	sizes<float_4>(scope, specs, nine);
	sizes<d::Frame<2>>(scope, specs, nine);
	sizes<d::Frame<8>>(scope, specs, nine);
	sizes<d::Frame<16>>(scope, specs, nine);
}

}  // namespace ring
}  // namespace dsp_bindings
}  // namespace rackpy
