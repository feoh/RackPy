// SPDX-License-Identifier: GPL-3.0-or-later
// common.hpp and arch.hpp, native parts: the APP_* configuration strings,
// bitCast, the user-defined literals, the arch macros, and the translation of
// rack::Exception. The semantic mappings that are plain Python (Exception,
// construct, get, DeferWrapper and the other macros) are in
// src/rackpy/raw/_common.py and src/rackpy/raw/macros.py.
#include "rackpy/runtime/buffers.hpp"
#include "rackpy/rtti.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "values.hpp"

namespace rackpy {
namespace values {

namespace {

enum class Prim { I8, U8, I16, U16, I32, U32, I64, U64, F32, F64 };

struct PrimInfo {
	const char* name;
	Prim prim;
	size_t size;
};

const PrimInfo PRIMS[] = {
	{"i8", Prim::I8, 1}, {"u8", Prim::U8, 1}, {"i16", Prim::I16, 2}, {"u16", Prim::U16, 2}, {"i32", Prim::I32, 4},
	{"u32", Prim::U32, 4}, {"f32", Prim::F32, 4}, {"i64", Prim::I64, 8}, {"u64", Prim::U64, 8}, {"f64", Prim::F64, 8},
};

const PrimInfo& prim(const std::string& name) {
	for (const PrimInfo& p : PRIMS) {
		if (name == p.name)
			return p;
	}
	throw py::value_error("bit_cast: type must be one of i8, u8, i16, u16, i32, u32, i64, u64, f32, f64, not " + name);
}

#if defined ARCH_WIN
/** What fopen_u8 returns: an owned FILE*, closed by close(), at the end of a with block, or when Python frees it.
Its C runtime is msvcrt, Rack's, which RackPy's MinGW DLLs use too, so fread and fclose here match _wfopen; and
errno is read here, in msvcrt, and handed to Python as a number (RP-42). Every call holds the GIL, so a close
cannot race a read. */
struct CFile {
	FILE* file = nullptr;
	CFile() = default;
	CFile(const CFile&) = delete;
	CFile& operator=(const CFile&) = delete;
	~CFile() {
		if (file)
			std::fclose(file);
	}
	FILE* open() const {
		if (!file)
			throw py::value_error("I/O operation on a closed CFile");
		return file;
	}
};

[[noreturn]] void raiseErrno(int error, py::object filename = py::none()) {
	py::object exc = py::reinterpret_borrow<py::object>(PyExc_OSError)(error, std::strerror(error), filename);
	PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(exc.ptr())), exc.ptr());
	throw py::error_already_set();
}

std::string noNul(const std::string& text, const char* what) {
	if (text.find('\0') != std::string::npos)
		throw py::value_error(std::string("fopen_u8: ") + what + " contains a NUL character");
	return text;
}
#endif

unsigned long long toU64(const py::int_& v, const char* what) {
	unsigned long long x = PyLong_AsUnsignedLongLong(v.ptr());
	if (x == (unsigned long long) -1 && PyErr_Occurred()) {
		PyErr_Clear();
		raiseOverflow(what, "unsigned long long");
	}
	return x;
}

template <typename T>
T fromPython(const py::handle& value, const char* type) {
	if constexpr (std::is_floating_point<T>::value) {
		return value.cast<T>();
	}
	else if constexpr (std::is_same<T, uint64_t>::value) {
		return toU64(py::reinterpret_borrow<py::int_>(value), "bit_cast");
	}
	else {
		if (!PyLong_Check(value.ptr()))
			throw py::type_error(std::string("bit_cast: an integer type (") + type + ") needs an int");
		return toInt<T>(py::reinterpret_borrow<py::int_>(value), "bit_cast", type);
	}
}

template <typename T>
py::object toPython(T value) {
	if constexpr (std::is_floating_point<T>::value)
		return py::float_((double) value);
	else
		return py::int_(value);
}

template <typename To, typename From>
py::object cast(const py::handle& value, const char* from) {
	// rack::bitCast static_asserts equal sizes, so only same-size pairs are instantiated;
	// the caller has already refused the others.
	if constexpr (sizeof(To) == sizeof(From))
		return toPython(rack::bitCast<To, From>(fromPython<From>(value, from)));
	else
		throw py::value_error("bit_cast: the types differ in size");
}

template <typename From>
py::object castFrom(const py::handle& value, Prim to, const char* from) {
	switch (to) {
		case Prim::I8: return cast<int8_t, From>(value, from);
		case Prim::U8: return cast<uint8_t, From>(value, from);
		case Prim::I16: return cast<int16_t, From>(value, from);
		case Prim::U16: return cast<uint16_t, From>(value, from);
		case Prim::I32: return cast<int32_t, From>(value, from);
		case Prim::U32: return cast<uint32_t, From>(value, from);
		case Prim::I64: return cast<int64_t, From>(value, from);
		case Prim::U64: return cast<uint64_t, From>(value, from);
		case Prim::F32: return cast<float, From>(value, from);
		case Prim::F64: return cast<double, From>(value, from);
	}
	throw py::value_error("bit_cast: unknown type");
}

}  // namespace

void bindCommon(py::module_ raw) {
	raw.attr("APP_NAME") = rack::APP_NAME;
	raw.attr("APP_EDITION") = rack::APP_EDITION;
	raw.attr("APP_EDITION_NAME") = rack::APP_EDITION_NAME;
	raw.attr("APP_VERSION_MAJOR") = rack::APP_VERSION_MAJOR;
	raw.attr("APP_VERSION") = rack::APP_VERSION;
	raw.attr("APP_OS") = rack::APP_OS;
	raw.attr("APP_OS_NAME") = rack::APP_OS_NAME;
	raw.attr("APP_CPU") = rack::APP_CPU;
	raw.attr("APP_CPU_NAME") = rack::APP_CPU_NAME;
	raw.attr("API_URL") = rack::API_URL;

	raw.def("bit_cast", [](py::object value, const std::string& to, const std::string& from) -> py::object {
		const PrimInfo& t = prim(to);
		const PrimInfo& f = prim(from);
		if (t.size != f.size)
			throw py::value_error("bit_cast: " + from + " and " + to + " differ in size (rack::bitCast requires the same size)");
		switch (f.prim) {
			case Prim::I8: return castFrom<int8_t>(value, t.prim, f.name);
			case Prim::U8: return castFrom<uint8_t>(value, t.prim, f.name);
			case Prim::I16: return castFrom<int16_t>(value, t.prim, f.name);
			case Prim::U16: return castFrom<uint16_t>(value, t.prim, f.name);
			case Prim::I32: return castFrom<int32_t>(value, t.prim, f.name);
			case Prim::U32: return castFrom<uint32_t>(value, t.prim, f.name);
			case Prim::I64: return castFrom<int64_t>(value, t.prim, f.name);
			case Prim::U64: return castFrom<uint64_t>(value, t.prim, f.name);
			case Prim::F32: return castFrom<float>(value, t.prim, f.name);
			case Prim::F64: return castFrom<double>(value, t.prim, f.name);
		}
		throw py::value_error("bit_cast: unknown type");
	}, py::arg("value"), py::arg("to"), py::arg("from_"),
		"Reinterprets value's bits as another primitive of the same size (rack::bitCast<To, From>). Types are "
		"i8, u8, i16, u16, i32, u32, i64, u64, f32 and f64.");

	// User-defined literals from common.hpp (global namespace): 42_i8 is literals.i8(42).
	py::module_ lit = raw.def_submodule("literals", "common.hpp's user-defined literals as functions: 42_u8 is u8(42).");
	lit.def("i8", [](const py::int_& x) { return operator""_i8(toU64(x, "i8")); }, py::arg("x"));
	lit.def("i16", [](const py::int_& x) { return operator""_i16(toU64(x, "i16")); }, py::arg("x"));
	lit.def("i32", [](const py::int_& x) { return operator""_i32(toU64(x, "i32")); }, py::arg("x"));
	lit.def("i64", [](const py::int_& x) { return operator""_i64(toU64(x, "i64")); }, py::arg("x"));
	lit.def("u8", [](const py::int_& x) { return operator""_u8(toU64(x, "u8")); }, py::arg("x"));
	lit.def("u16", [](const py::int_& x) { return operator""_u16(toU64(x, "u16")); }, py::arg("x"));
	lit.def("u32", [](const py::int_& x) { return operator""_u32(toU64(x, "u32")); }, py::arg("x"));
	lit.def("u64", [](const py::int_& x) { return operator""_u64(toU64(x, "u64")); }, py::arg("x"));
	// The integer overloads first, so f32(3) reaches operator""_f32(unsigned long long).
	lit.def("f32", [](const py::int_& x) { return operator""_f32(toU64(x, "f32")); }, py::arg("x"));
	lit.def("f32", [](double x) { return operator""_f32((long double) x); }, py::arg("x"));
	lit.def("f64", [](const py::int_& x) { return operator""_f64(toU64(x, "f64")); }, py::arg("x"));
	lit.def("f64", [](double x) { return operator""_f64((long double) x); }, py::arg("x"));

	// arch.hpp: defined only on the platform the plugin was built for.
	py::module_ arch = raw.def_submodule("_arch", "arch.hpp's macros as built; see rackpy.raw.macros.");
#if defined ARCH_X64
	arch.attr("ARCH_X64") = ARCH_X64;
#endif
#if defined ARCH_ARM64
	arch.attr("ARCH_ARM64") = ARCH_ARM64;
#endif
#if defined ARCH_WIN
	arch.attr("ARCH_WIN") = ARCH_WIN;
#endif
#if defined ARCH_MAC
	arch.attr("ARCH_MAC") = ARCH_MAC;
#endif
#if defined ARCH_LIN
	arch.attr("ARCH_LIN") = ARCH_LIN;
#endif

#if defined ARCH_WIN
	// common.hpp on Windows: fopen_u8 (and `#define fopen fopen_u8`, rackpy.raw.macros.fopen) opens a file by its
	// UTF-8 name, where the C runtime's own fopen reads names in the ANSI code page.
	py::class_<CFile>(raw, "CFile", "fopen_u8's FILE* (Windows only): owned, closed by close() or a with block.")
		.def("read", [](CFile& f, long long size) {
			FILE* file = f.open();
			std::string out;
			char buffer[65536];
			while (size < 0 || (long long) out.size() < size) {
				size_t want = size < 0 ? sizeof(buffer) : std::min(sizeof(buffer), (size_t) (size - out.size()));
				size_t got = std::fread(buffer, 1, want, file);
				out.append(buffer, got);
				if (got < want) {
					if (std::ferror(file))
						raiseErrno(errno);
					break;
				}
			}
			return py::bytes(out);
		}, py::arg("size") = -1, "fread: up to size bytes (all of them if negative); b'' at the end of the file.")
		.def("write", [](CFile& f, py::buffer data) {
			py::buffer_info info = data.request();
			if (info.ndim != 1 || info.itemsize != 1)
				throw py::type_error("CFile.write takes bytes (a 1-D byte buffer)");
			runtime::requireContiguous(info, "CFile.write");
			size_t wrote = std::fwrite(info.ptr, 1, (size_t) info.size, f.open());
			if (wrote < (size_t) info.size)
				raiseErrno(errno);
			return wrote;
		}, py::arg("data"), "fwrite: writes all of data, or raises OSError; returns its length.")
		.def("seek", [](CFile& f, long long offset, int whence) {
			if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
				throw py::value_error("CFile.seek: whence must be 0, 1 or 2");
			if (_fseeki64(f.open(), offset, whence) != 0)
				raiseErrno(errno);
			return (long long) _ftelli64(f.file);
		}, py::arg("offset"), py::arg("whence") = 0, "fseek (64-bit); returns the new position.")
		.def("tell", [](CFile& f) { return (long long) _ftelli64(f.open()); }, "ftell (64-bit).")
		.def("flush", [](CFile& f) {
			if (std::fflush(f.open()) != 0)
				raiseErrno(errno);
		}, "fflush.")
		.def("close", [](CFile& f) {
			if (!f.file)
				return;
			FILE* file = f.file;
			f.file = nullptr;
			if (std::fclose(file) != 0)
				raiseErrno(errno);
		}, "fclose; closing twice does nothing.")
		.def_property_readonly("closed", [](const CFile& f) { return f.file == nullptr; })
		.def("__enter__", [](CFile& f) -> CFile& {
			f.open();
			return f;
		}, py::return_value_policy::reference)
		.def("__exit__", [](CFile& f, py::args) {
			if (f.file) {
				std::fclose(f.file);
				f.file = nullptr;
			}
		});
	raw.def("fopen_u8", [](const std::string& filename, const std::string& mode) {
		std::unique_ptr<CFile> f(new CFile());
		errno = 0;
		f->file = fopen_u8(noNul(filename, "the file name").c_str(), noNul(mode, "the mode").c_str());
		if (!f->file)
			raiseErrno(errno ? errno : EINVAL, py::str(filename));
		return f;
	}, py::arg("filename"), py::arg("mode"),
		"Rack's (Windows only): _wfopen of the UTF-8 file name and mode. A CFile; OSError (with the C runtime's "
		"errno) where C returns NULL.");
#endif

	// rack::Exception thrown by Rack becomes rackpy.raw.Exception (defined in Python, src/rackpy/raw/_common.py).
	// Caught as std::exception and identified by rttiCast: rack::Exception has no key function, so libRack
	// and the host each have a type_info for it, and on macOS a catch clause naming the host's copy does not
	// match what libRack throws (RP-40). std::exception's type_info is the C++ library's own, one copy.
	py::register_exception_translator([](std::exception_ptr p) {
		try {
			if (p)
				std::rethrow_exception(p);
		}
		catch (const std::exception& e) {
			const auto* rackException = ::rackpy::rttiCast<const rack::Exception*>(&e);
			if (!rackException)
				throw;
			py::object type = py::module_::import("rackpy.raw").attr("Exception");
			PyErr_SetObject(type.ptr(), py::str(rackException->msg).ptr());
		}
	});
}

}  // namespace values
}  // namespace rackpy
