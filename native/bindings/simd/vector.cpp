// SPDX-License-Identifier: GPL-3.0-or-later
// rack::simd::Vector (Vector.hpp) -> rackpy.raw.simd.float_4 and int32_4.
//
// Each vector is a Python value holding Rack's C++ vector; every operator,
// constructor, load and store calls Rack's own inline code, compiled with
// the SDK's flags (tests/simd compares the lanes with a compiled C++
// reference). Comparisons return masks, as in C++: a vector whose lanes are
// all ones (true) or all zeros (false). Python cannot treat a mask as a
// bool, so `if a < b:` raises TypeError; use simd.movemask.
//
// As in C++, a scalar broadcasts where a vector is expected (a Python float
// or int to float_4, an int to int32_4), but float_4 and int32_4 never
// convert into each other implicitly: C++ rejects `float_4 + int32_4` as
// ambiguous, so Python raises TypeError. Convert with float_4(i) or
// int32_4(f), or reinterpret the bits with cast().
#include "simd.hpp"

#include <pybind11/operators.h>

#include <string>

#include "../values/values.hpp"

namespace rackpy {
namespace simd_bindings {

using rack::simd::float_4;
using rack::simd::int32_4;

namespace {

int lane(int i) {
	if (i < 0)
		i += 4;
	if (i < 0 || i >= 4)
		throw py::index_error("a vector has 4 lanes: the index must be 0 to 3, or -4 to -1");
	return i;
}

int32_t i32(const py::int_& v, const char* what) {
	return values::toInt<int32_t>(v, what, "int32_t");
}

int cint(const py::int_& v, const char* what) {
	return values::toInt<int>(v, what, "int");
}

std::string repr(const float_4& v) {
	std::string out = "float_4(";
	for (int k = 0; k < 4; k++)
		out += (k ? ", " : "") + values::floatRepr(v.s[k]);
	return out + ")";
}

std::string repr(const int32_4& v) {
	std::string out = "int32_4(";
	for (int k = 0; k < 4; k++)
		out += (k ? ", " : "") + std::to_string(v.s[k]);
	return out + ")";
}

template <typename T>
const char* itemName();
template <>
const char* itemName<float>() {
	return "float32 ('f')";
}
template <>
const char* itemName<int32_t>() {
	return "int32 ('i')";
}

/** The first 4 items of a contiguous 1-D buffer of T. Rack's load and store use unaligned
accesses, so any alignment is fine; a short buffer would be read or written past its end. */
template <typename T>
T* lanesOf(const py::buffer& buf, bool writable, const char* what) {
	py::buffer_info info = buf.request(writable);  // BufferError for a read-only buffer when writing
	if (!info.item_type_is_equivalent_to<T>())
		throw py::type_error(std::string(what) + " needs a buffer of " + itemName<T>() + " items, not '" + info.format + "'");
	if (info.ndim != 1 || info.strides[0] != (py::ssize_t) sizeof(T))
		throw py::value_error(std::string(what) + " needs a contiguous one-dimensional buffer");
	if (info.size < 4)
		throw py::value_error(std::string(what) + " needs at least 4 items; the buffer has " + std::to_string(info.size));
	return static_cast<T*>(info.ptr);
}

/** A C++ template that Python specializes by subscripting: Vector[float, 4], movemask_inverse[float_4]. */
struct Template {
	std::string name;
	py::dict specializations;  // (template arguments...) -> class or function
};

py::object specialize(const Template& t, py::object args) {
	py::tuple key = py::isinstance<py::tuple>(args) ? args.cast<py::tuple>() : py::make_tuple(args);
	if (!t.specializations.contains(key)) {
		std::string have;
		for (auto item : t.specializations)
			have += (have.empty() ? "" : ", ") + t.name + py::repr(item.first).cast<std::string>();
		throw py::type_error(t.name + py::repr(key).cast<std::string>() + " is not defined in Rack; it defines " + have);
	}
	return t.specializations[key];
}

template <typename V, typename Class>
void valueProtocol(Class& c) {
	c.def("__iter__", [](const V& v) { return py::iter(py::make_tuple(v.s[0], v.s[1], v.s[2], v.s[3])); })
		.def("__len__", [](const V&) { return 4; })
		.def("tolist", [](const V& v) {
			py::list out;
			for (int k = 0; k < 4; k++)
				out.append(v.s[k]);
			return out;
		}, "The lanes as a list.")
		.def("__copy__", [](const V& v) { return v; })
		.def("__deepcopy__", [](const V& v, py::dict) { return v; }, py::arg("memo"))
		.def("__repr__", [](const V& v) { return repr(v); })
		.def("__bool__", [](const V&) -> bool {
			throw py::type_error("the truth value of a vector is ambiguous: comparisons give masks; use simd.movemask");
		})
		.def_property_readonly("s", [](py::object self) { return py::memoryview(self); },
			"The lanes as a live, writable memoryview (Vector::s, a union with v); it keeps the vector alive.")
		.def_property("v", py::cpp_function([](const V& v) { return V(v.v); }),
			py::cpp_function([](V& self, const V& v) { self.v = v.v; }, py::is_method(c), py::arg("value").noconvert()),
			"The native register (Vector::v, a union with s), as a vector of the same type: reading copies it, "
			"assigning replaces every lane.");
	c.attr("size") = V::size;
	c.attr("__hash__") = py::none();
}

}  // namespace

void bindVectors(py::module_ m) {
	py::class_<Template>(m, "Template", "A C++ template (Vector, movemask_inverse): subscript it with its template arguments.")
		.def("__getitem__", &specialize, py::arg("arguments"))
		.def("__call__", [](const Template& t, py::args, py::kwargs) -> py::object {
			throw py::type_error(t.name + " is a C++ template: specialize it first, " + t.name + "[...]");
		})
		.def_property_readonly("specializations", [](const Template& t) { return t.specializations.attr("copy")(); })
		.def("__repr__", [](const Template& t) { return "<C++ template rack::simd::" + t.name + ">"; });

	py::class_<float_4> f(m, "float_4", py::buffer_protocol(),
		"rack::simd::float_4 (Vector<float, 4>): 4 float lanes in one SSE register. A mutable value.");
	py::class_<int32_4> n(m, "int32_4", py::buffer_protocol(),
		"rack::simd::int32_4 (Vector<int32_t, 4>): 4 int32 lanes in one SSE register. A mutable value.");

	// --- float_4 --------------------------------------------------------------------
	// The float overloads come first so that float_4(2) broadcasts 2.0 rather than converting an int32_4.
	f.def(py::init([] { return float_4::zero(); }), "All lanes 0. (Rack's Vector() leaves them uninitialized.)")
		.def(py::init<float>(), py::arg("x"), "Every lane x.")
		.def(py::init<float, float, float, float>(), py::arg("x1"), py::arg("x2"), py::arg("x3"), py::arg("x4"))
		.def(py::init([](const float_4& v) { return float_4(v.v); }), py::arg("v").noconvert(),
			"A copy: Rack's Vector(__m128), with float_4 standing for __m128.")
		.def(py::init([](const int32_4& a) { return float_4(a); }), py::arg("a").noconvert(),
			"Converts each int32 lane to the nearest float (_mm_cvtepi32_ps).")
		.def_static("zero", &float_4::zero, "All bits 0.")
		.def_static("mask", &float_4::mask, "All bits 1 (every lane true; each lane is a NaN as a float).")
		.def_static("load", [](const py::buffer& x) { return float_4::load(lanesOf<float>(x, false, "float_4.load")); },
			py::arg("x"), "The first 4 floats of a buffer, x[0] in lane 0. The buffer must hold float32 items.")
		.def("store", [](float_4& v, const py::buffer& x) { v.store(lanesOf<float>(x, true, "float_4.store")); },
			py::arg("x"), "Writes the lanes into the first 4 items of a writable float32 buffer.")
		.def_static("cast", &float_4::cast, py::arg("a"), "Reinterprets an int32_4's bits as floats (no conversion).")
		.def("__getitem__", [](const float_4& v, int i) { return static_cast<const float_4&>(v)[lane(i)]; }, py::arg("i"))
		.def("__setitem__", [](float_4& v, int i, float x) { v[lane(i)] = x; }, py::arg("i"), py::arg("x"))
		.def_buffer([](float_4& v) {
			return py::buffer_info(v.s, sizeof(float), py::format_descriptor<float>::format(), 1, {4}, {sizeof(float)});
		})
		// Rack's free operators.
		.def("__add__", [](const float_4& a, const float_4& b) { return a + b; }, py::is_operator())
		.def("__radd__", [](const float_4& b, const float_4& a) { return a + b; }, py::is_operator())
		.def("__sub__", [](const float_4& a, const float_4& b) { return a - b; }, py::is_operator())
		.def("__rsub__", [](const float_4& b, const float_4& a) { return a - b; }, py::is_operator())
		.def("__mul__", [](const float_4& a, const float_4& b) { return a * b; }, py::is_operator())
		.def("__rmul__", [](const float_4& b, const float_4& a) { return a * b; }, py::is_operator())
		.def("__truediv__", [](const float_4& a, const float_4& b) { return a / b; }, py::is_operator())
		.def("__rtruediv__", [](const float_4& b, const float_4& a) { return a / b; }, py::is_operator())
		.def("__xor__", [](const float_4& a, const float_4& b) { return a ^ b; }, py::is_operator())
		.def("__rxor__", [](const float_4& b, const float_4& a) { return a ^ b; }, py::is_operator())
		.def("__and__", [](const float_4& a, const float_4& b) { return a & b; }, py::is_operator())
		.def("__rand__", [](const float_4& b, const float_4& a) { return a & b; }, py::is_operator())
		.def("__or__", [](const float_4& a, const float_4& b) { return a | b; }, py::is_operator())
		.def("__ror__", [](const float_4& b, const float_4& a) { return a | b; }, py::is_operator())
		.def("__iadd__", [](py::object self, const float_4& b) { self.cast<float_4&>() += b; return self; }, py::is_operator())
		.def("__isub__", [](py::object self, const float_4& b) { self.cast<float_4&>() -= b; return self; }, py::is_operator())
		.def("__imul__", [](py::object self, const float_4& b) { self.cast<float_4&>() *= b; return self; }, py::is_operator())
		.def("__itruediv__", [](py::object self, const float_4& b) { self.cast<float_4&>() /= b; return self; }, py::is_operator())
		.def("__ixor__", [](py::object self, const float_4& b) { self.cast<float_4&>() ^= b; return self; }, py::is_operator())
		.def("__iand__", [](py::object self, const float_4& b) { self.cast<float_4&>() &= b; return self; }, py::is_operator())
		.def("__ior__", [](py::object self, const float_4& b) { self.cast<float_4&>() |= b; return self; }, py::is_operator())
		.def("__eq__", [](const float_4& a, const float_4& b) { return a == b; }, py::is_operator())
		.def("__ne__", [](const float_4& a, const float_4& b) { return a != b; }, py::is_operator())
		.def("__lt__", [](const float_4& a, const float_4& b) { return a < b; }, py::is_operator())
		.def("__le__", [](const float_4& a, const float_4& b) { return a <= b; }, py::is_operator())
		.def("__gt__", [](const float_4& a, const float_4& b) { return a > b; }, py::is_operator())
		.def("__ge__", [](const float_4& a, const float_4& b) { return a >= b; }, py::is_operator())
		.def("__pos__", [](const float_4& a) { return +a; })
		.def("__neg__", [](const float_4& a) { return -a; })
		.def("__invert__", [](const float_4& a) { return ~a; })
		.def("pre_increment", [](py::object self) { ++self.cast<float_4&>(); return self; },
			"C++ ++a: adds 1 to every lane and returns this vector.")
		.def("pre_decrement", [](py::object self) { --self.cast<float_4&>(); return self; },
			"C++ --a: subtracts 1 from every lane and returns this vector.")
		.def("post_increment", [](float_4& a) { return a++; }, "C++ a++: adds 1 to every lane and returns the old value.")
		.def("post_decrement", [](float_4& a) { return a--; }, "C++ a--: subtracts 1 from every lane and returns the old value.");
	valueProtocol<float_4>(f);
	f.attr("type") = py::module_::import("builtins").attr("float");

	// --- int32_4 --------------------------------------------------------------------
	n.def(py::init([] { return int32_4::zero(); }), "All lanes 0. (Rack's Vector() leaves them uninitialized.)")
		.def(py::init([](const py::int_& x) { return int32_4(i32(x, "int32_4")); }), py::arg("x"), "Every lane x.")
		.def(py::init([](const py::int_& a, const py::int_& b, const py::int_& c, const py::int_& d) {
			return int32_4(i32(a, "int32_4"), i32(b, "int32_4"), i32(c, "int32_4"), i32(d, "int32_4"));
		}), py::arg("x1"), py::arg("x2"), py::arg("x3"), py::arg("x4"))
		.def(py::init([](const int32_4& v) { return int32_4(v.v); }), py::arg("v").noconvert(),
			"A copy: Rack's Vector(__m128i), with int32_4 standing for __m128i.")
		.def(py::init([](const float_4& a) { return int32_4(a); }), py::arg("a").noconvert(),
			"Converts each float lane toward zero (_mm_cvttps_epi32); NaN and out-of-range lanes give -2**31.")
		.def_static("zero", &int32_4::zero, "All bits 0.")
		.def_static("mask", &int32_4::mask, "All bits 1 (every lane -1, true).")
		.def_static("load", [](const py::buffer& x) { return int32_4::load(lanesOf<int32_t>(x, false, "int32_4.load")); },
			py::arg("x"), "The first 4 items of a buffer of int32, x[0] in lane 0.")
		.def("store", [](int32_4& v, const py::buffer& x) { v.store(lanesOf<int32_t>(x, true, "int32_4.store")); },
			py::arg("x"), "Writes the lanes into the first 4 items of a writable int32 buffer.")
		.def_static("cast", &int32_4::cast, py::arg("a"), "Reinterprets a float_4's bits as int32 (no conversion).")
		.def("__getitem__", [](const int32_4& v, int i) { return static_cast<const int32_4&>(v)[lane(i)]; }, py::arg("i"))
		.def("__setitem__", [](int32_4& v, int i, const py::int_& x) { v[lane(i)] = i32(x, "int32_4 lane"); }, py::arg("i"),
			py::arg("x"))
		.def_buffer([](int32_4& v) {
			return py::buffer_info(v.s, sizeof(int32_t), py::format_descriptor<int32_t>::format(), 1, {4}, {sizeof(int32_t)});
		})
		.def("__add__", [](const int32_4& a, const int32_4& b) { return a + b; }, py::is_operator())
		.def("__radd__", [](const int32_4& b, const int32_4& a) { return a + b; }, py::is_operator())
		.def("__sub__", [](const int32_4& a, const int32_4& b) { return a - b; }, py::is_operator())
		.def("__rsub__", [](const int32_4& b, const int32_4& a) { return a - b; }, py::is_operator())
		.def("__xor__", [](const int32_4& a, const int32_4& b) { return a ^ b; }, py::is_operator())
		.def("__rxor__", [](const int32_4& b, const int32_4& a) { return a ^ b; }, py::is_operator())
		.def("__and__", [](const int32_4& a, const int32_4& b) { return a & b; }, py::is_operator())
		.def("__rand__", [](const int32_4& b, const int32_4& a) { return a & b; }, py::is_operator())
		.def("__or__", [](const int32_4& a, const int32_4& b) { return a | b; }, py::is_operator())
		.def("__ror__", [](const int32_4& b, const int32_4& a) { return a | b; }, py::is_operator())
		.def("__iadd__", [](py::object self, const int32_4& b) { self.cast<int32_4&>() += b; return self; }, py::is_operator())
		.def("__isub__", [](py::object self, const int32_4& b) { self.cast<int32_4&>() -= b; return self; }, py::is_operator())
		.def("__ixor__", [](py::object self, const int32_4& b) { self.cast<int32_4&>() ^= b; return self; }, py::is_operator())
		.def("__iand__", [](py::object self, const int32_4& b) { self.cast<int32_4&>() &= b; return self; }, py::is_operator())
		.def("__ior__", [](py::object self, const int32_4& b) { self.cast<int32_4&>() |= b; return self; }, py::is_operator())
		.def("__eq__", [](const int32_4& a, const int32_4& b) { return a == b; }, py::is_operator())
		.def("__ne__", [](const int32_4& a, const int32_4& b) { return a != b; }, py::is_operator())
		.def("__lt__", [](const int32_4& a, const int32_4& b) { return a < b; }, py::is_operator())
		.def("__le__", [](const int32_4& a, const int32_4& b) { return a <= b; }, py::is_operator())
		.def("__gt__", [](const int32_4& a, const int32_4& b) { return a > b; }, py::is_operator())
		.def("__ge__", [](const int32_4& a, const int32_4& b) { return a >= b; }, py::is_operator())
		// Shifts are logical (_mm_sll/_mm_srl_epi32): a count outside 0 to 31 gives 0 in every lane.
		.def("__lshift__", [](const int32_4& a, const py::int_& b) { return a << cint(b, "int32_4 <<"); }, py::is_operator())
		.def("__rshift__", [](const int32_4& a, const py::int_& b) { return a >> cint(b, "int32_4 >>"); }, py::is_operator())
		.def("__pos__", [](const int32_4& a) { return +a; })
		.def("__neg__", [](const int32_4& a) { return -a; })
		.def("__invert__", [](const int32_4& a) { return ~a; })
		.def("pre_increment", [](py::object self) { ++self.cast<int32_4&>(); return self; },
			"C++ ++a: adds 1 to every lane (wrapping) and returns this vector.")
		.def("pre_decrement", [](py::object self) { --self.cast<int32_4&>(); return self; },
			"C++ --a: subtracts 1 from every lane (wrapping) and returns this vector.")
		.def("post_increment", [](int32_4& a) { return a++; }, "C++ a++: adds 1 to every lane and returns the old value.")
		.def("post_decrement", [](int32_4& a) { return a--; }, "C++ a--: subtracts 1 from every lane and returns the old value.");
	valueProtocol<int32_4>(n);
	n.attr("type") = py::module_::import("builtins").attr("int");

	// A scalar broadcasts where a vector is expected, as C++'s Vector(float) and Vector(int32_t) do.
	py::implicitly_convertible<py::float_, float_4>();
	py::implicitly_convertible<py::int_, float_4>();
	py::implicitly_convertible<py::int_, int32_4>();

	// The primary template Vector<TYPE, SIZE> is declared but not defined; these two specializations exist.
	Template vector{"Vector", py::dict()};
	vector.specializations[py::make_tuple(py::module_::import("builtins").attr("float"), 4)] = f;
	vector.specializations[py::make_tuple(py::module_::import("builtins").attr("int"), 4)] = n;
	m.attr("Vector") = py::cast(std::move(vector));

	// Which branch of simd/common.hpp this plugin was compiled with (rackpy.simd.IMPLEMENTATION).
#ifdef __SSE4_2__
	m.attr("_IMPLEMENTATION") = "sse4.2";
#else
	m.attr("_IMPLEMENTATION") = "simde";
#endif
}

void addTemplate(py::module_ m, const char* name, py::dict specializations) {
	m.attr(name) = py::cast(Template{name, std::move(specializations)});
}

}  // namespace simd_bindings
}  // namespace rackpy
