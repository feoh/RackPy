// SPDX-License-Identifier: GPL-3.0-or-later
// rack::random -> rackpy.raw.random (RP-30A). Rack's generator (random::local)
// is one process-wide Xoroshiro128Plus with no lock: Rack's engine threads and
// UI use it alike, so draws from Python race with theirs as Rack's own do.
// Make an Xoroshiro128Plus of your own for reproducible numbers.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <vector>

#include <rack.hpp>

#include "services.hpp"

namespace rackpy {
namespace bindings {
namespace services {

namespace py = pybind11;
namespace r = rack::random;

void bindRandom(py::module_ m) {
	py::class_<r::Xoroshiro128Plus>(m, "Xoroshiro128Plus",
		"Rack's xoroshiro128+ generator, as a value. Calling it returns the next 64-bit number.")
		.def(py::init<>(), "Unseeded (both state words zero): it returns zeros until seeded.")
		.def(py::init<uint64_t, uint64_t>(), py::arg("s0"), py::arg("s1") = 0, "Rack's: seeds it, then draws one number.")
		.def_property("state",
			[](const r::Xoroshiro128Plus& g) { return std::vector<uint64_t>{g.state[0], g.state[1]}; },
			[](r::Xoroshiro128Plus& g, std::vector<uint64_t> state) {
				if (state.size() != 2)
					throw py::value_error("the state is two 64-bit words");
				g.state[0] = state[0];
				g.state[1] = state[1];
			}, "Rack's two state words, as a list (assign two ints).")
		.def("seed", &r::Xoroshiro128Plus::seed, py::arg("s0"), py::arg("s1") = 0, "Rack's: sets the state, then draws one.")
		.def("is_seeded", &r::Xoroshiro128Plus::isSeeded)
		.def("__call__", &r::Xoroshiro128Plus::operator(), "Rack's operator(): the next number, 0 to 2**64 - 1.")
		.def_static("rotl", [](uint64_t x, int k) {
			if (k < 1 || k > 63)
				throw py::value_error("Rack's rotl shifts by 1 to 63 (0 and 64 would shift by 64, undefined in C++)");
			return r::Xoroshiro128Plus::rotl(x, k);
		}, py::arg("x"), py::arg("k"))
		.def_static("min", &r::Xoroshiro128Plus::min)
		.def_static("max", &r::Xoroshiro128Plus::max)
		.def("__copy__", [](const r::Xoroshiro128Plus& g) { return g; })
		.def("__repr__", [](const r::Xoroshiro128Plus& g) {
			return "Xoroshiro128Plus(state=[" + std::to_string(g.state[0]) + ", " + std::to_string(g.state[1]) + "])";
		});
	m.attr("Xoroshiro128Plus").attr("result_type") = py::module_::import("builtins").attr("int");

	m.def("local", [] { return &r::local(); }, py::return_value_policy::reference,
		"Rack's generator: one for the whole process, which lives until Rack quits. Changing it changes Rack's.");
	m.def("init", &r::init, "Rack's: seeds Rack's generator from the clock and the system's randomness.");
	m.def("get", [](const std::string& kind) -> py::object {
		if (kind == "uint64") return py::int_(r::get<uint64_t>());
		if (kind == "uint32") return py::int_(r::get<uint32_t>());
		if (kind == "uint16") return py::int_(r::get<uint16_t>());
		if (kind == "uint8") return py::int_(r::get<uint8_t>());
		// Rack's primary template: the next 64-bit number converted to T (modulo 2**N, as GCC defines it).
		if (kind == "int64") return py::int_(r::get<int64_t>());
		if (kind == "int32") return py::int_(r::get<int32_t>());
		if (kind == "int16") return py::int_(r::get<int16_t>());
		if (kind == "int8") return py::int_(r::get<int8_t>());
		if (kind == "bool") return py::bool_(r::get<bool>());
		if (kind == "float") return py::float_(r::get<float>());
		if (kind == "double") return py::float_(r::get<double>());
		throw py::value_error(
			"Rack's get<T> takes uint64, uint32, uint16, uint8, int64, int32, int16, int8, bool, float or double");
	}, py::arg("type") = "uint64", "Rack's get<T> from its generator, by the C++ type's name.");
	m.def("get_uint32", [] { return r::get<uint32_t>(); }, "Rack's get<uint32_t>: the top 32 bits.");
	m.def("get_uint16", [] { return r::get<uint16_t>(); });
	m.def("get_uint8", [] { return r::get<uint8_t>(); });
	m.def("get_bool", [] { return r::get<bool>(); });
	m.def("get_float", [] { return r::get<float>(); }, "Rack's get<float>: [0, 1).");
	m.def("get_double", [] { return r::get<double>(); });
	m.def("u64", &r::u64);
	m.def("u32", &r::u32);
	m.def("uniform", &r::uniform, "Rack's: a float in [0, 1).");
	m.def("normal", &r::normal, "Rack's: a normal float, mean 0 and standard deviation 1.");
	m.def("buffer", [](py::buffer out) {
		py::buffer_info info = out.request(true);
		if (info.readonly || info.itemsize != 1 || info.ndim != 1)
			throw py::type_error("buffer fills a writable 1-D byte buffer (a bytearray, say)");
		r::buffer(static_cast<uint8_t*>(info.ptr), static_cast<size_t>(info.size));
	}, py::arg("out"), "Rack's: fills the buffer with random bytes.");
	m.def("vector", [](size_t len) {
		std::vector<uint8_t> v = r::vector(len);
		return py::bytes(reinterpret_cast<const char*>(v.data()), v.size());
	}, py::arg("len"), "Rack's: len random bytes.");
}

}  // namespace services
}  // namespace bindings
}  // namespace rackpy
