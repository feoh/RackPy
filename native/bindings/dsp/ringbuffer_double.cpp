// SPDX-License-Identifier: GPL-3.0-or-later
// dsp/ringbuffer.hpp -> rackpy.raw.dsp (RP-19C): DoubleRingBuffer<T, S>.
//
// Rack keeps a copy of the buffer after it, so end_data() and start_data() give
// S consecutive elements at any position: views of the inline data, which
// never moves, so they stay valid (if stale, as Rack documents) for as long as
// they keep the buffer alive. RackPy refuses end_incr past S, which would copy
// past the data; the rest follows Rack (push overwrites when full, shift and
// start_incr can pass end).
#include "ringbuffer.hpp"

#include <memory>
#include <utility>

namespace rackpy {
namespace dsp_bindings {
namespace ring {
namespace {

namespace c = dsp_calls;

template <typename T, size_t S>
void bindOne(py::module_ scope, py::dict& specs) {
	using R = d::DoubleRingBuffer<T, S>;
	using E = Element<T>;
	std::string name = className<R>("DoubleRingBuffer", E::name(), {S});
	py::class_<R> cls(scope, name.c_str(), ("rack::dsp::DoubleRingBuffer<" + E::name() + ", " + std::to_string(S) +
		">: a cyclic buffer of " + std::to_string(S) + " elements kept twice, so any S consecutive ones are linear. Not thread-safe.").c_str());
	cls.attr("S") = S;
	cls.def(py::init([] { return std::unique_ptr<R>(c::make<R>()); }), "Empty, with zeroed data.")
		.def_property("start", [](const R& r) { return r.start.load(); }, [](R& r, size_t v) { r.start = v; },
			"std::atomic<size_t>; Rack takes it modulo S.")
		.def_property("end", [](const R& r) { return r.end.load(); }, [](R& r, size_t v) { r.end = v; },
			"std::atomic<size_t>; Rack takes it modulo S.")
		.def_property_readonly("data", [](py::object self) { return elements<T>(self, self.cast<R&>().data, 2 * S, false); },
			"T data[2 S] (a live view; Frame elements as floats).")
		.def("push", [](R& r, py::handle t) { c::member<&R::push>(r, E::from(t)); }, py::arg("t"))
		.def("shift", [](R& r) { return E::to(c::member<&R::shift>(r)); })
		.def("clear", [](R& r) { c::member<&R::clear>(r); })
		.def("empty", [](const R& r) { return c::member<&R::empty>(r); })
		.def("full", [](const R& r) { return c::member<&R::full>(r); })
		.def("size", [](const R& r) { return c::member<&R::size>(r); })
		.def("capacity", [](const R& r) { return c::member<&R::capacity>(r); })
		.def("end_data", [](py::object self) { return elements<T>(self, c::member<&R::endData>(self.cast<R&>()), S, false); },
			"S writable elements at the end, for appending (a view); call end_incr afterwards.")
		.def("end_incr", [](R& r, size_t n) {
			if (n > S)
				throw py::value_error("end_incr: n must be at most S (" + std::to_string(S) + "); Rack would copy past its data");
			c::member<&R::endIncr>(r, n);
		}, py::arg("n"), "Commits n appended elements, copying them to the other half.")
		.def("start_data", [](py::object self) {
			return elements<T>(self, c::member<&R::startData>(self.cast<const R&>()), S, true);
		}, "S elements from the start, for consumption (a read-only view: C++ returns const T*).")
		.def("start_incr", [](R& r, size_t n) { c::member<&R::startIncr>(r, n); }, py::arg("n"), "Consumes n elements.");
	specs[py::make_tuple(E::key(), S)] = cls;
}

template <typename T, size_t... K>
void sizes(py::module_ scope, py::dict& specs, std::index_sequence<K...>) {
	(bindOne<T, (size_t(16) << K)>(scope, specs), ...);
}

}  // namespace

void bindDouble(py::module_ scope, py::dict& specs) {
	auto twelve = std::make_index_sequence<12>();  // S = 16 to 32768
	sizes<float>(scope, specs, twelve);
	sizes<float_4>(scope, specs, twelve);
	sizes<d::Frame<2>>(scope, specs, twelve);
	sizes<d::Frame<8>>(scope, specs, twelve);
	sizes<d::Frame<16>>(scope, specs, twelve);
}

}  // namespace ring
}  // namespace dsp_bindings
}  // namespace rackpy
