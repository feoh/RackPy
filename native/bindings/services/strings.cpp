// SPDX-License-Identifier: GPL-3.0-or-later
// rack::string -> rackpy.raw.string (RP-30A).
//
// string::f is printf: a variadic C call whose argument types the format
// decides, which Python cannot make. formatPrintf parses the format, checks
// each conversion against its Python argument and converts it to the C type
// the conversion and its length modifier name, then calls Rack's string::f on
// that single conversion: Rack's own formatting, one checked argument at a
// time. %n, %p and * widths are refused; a wrong count or type raises.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <vector>

#include <rack.hpp>

#include "rackpy/runtime/threads.hpp"
#include "services.hpp"

namespace rackpy {
namespace bindings {
namespace services {

namespace py = pybind11;
namespace s = rack::string;

namespace {

template <class T>
T integer(py::handle value, const std::string& spec) {
	if (!py::isinstance<py::int_>(value) || py::isinstance<py::bool_>(value))
		throw py::type_error("%" + spec + " needs an int, not " + std::string(py::str(py::type::of(value))));
	py::int_ v = py::reinterpret_borrow<py::int_>(value);
	try {
		if constexpr (std::is_signed<T>::value) {
			long long x = v.cast<long long>();
			if (x < static_cast<long long>(std::numeric_limits<T>::min()) || x > static_cast<long long>(std::numeric_limits<T>::max()))
				throw py::value_error("");
			return static_cast<T>(x);
		}
		else {
			unsigned long long x = v.cast<unsigned long long>();
			if (x > static_cast<unsigned long long>(std::numeric_limits<T>::max()))
				throw py::value_error("");
			return static_cast<T>(x);
		}
	}
	catch (const std::exception&) {
		throw py::value_error("%" + spec + " cannot represent " + std::string(py::str(value)));
	}
}

std::string bytesOf(py::handle value, const std::string& spec) {
	if (py::isinstance<py::str>(value))
		return value.cast<std::string>();
	if (py::isinstance<py::bytes>(value))
		return value.cast<std::string>();
	throw py::type_error("%" + spec + " needs a str or bytes, not " + std::string(py::str(py::type::of(value))));
}

/** One conversion, through Rack's string::f with the C type the format names. */
std::string convert(const std::string& spec, const std::string& length, char conv, py::handle value) {
	std::string format = "%" + spec;
	const char* f = format.c_str();
	switch (conv) {
		case 'd':
		case 'i':
			if (length == "hh") return s::f(f, static_cast<int>(integer<signed char>(value, spec)));
			if (length == "h") return s::f(f, static_cast<int>(integer<short>(value, spec)));
			if (length == "") return s::f(f, integer<int>(value, spec));
			if (length == "l") return s::f(f, integer<long>(value, spec));
			if (length == "ll") return s::f(f, integer<long long>(value, spec));
			if (length == "j") return s::f(f, integer<intmax_t>(value, spec));
			if (length == "z") return s::f(f, integer<ssize_t>(value, spec));
			if (length == "t") return s::f(f, integer<ptrdiff_t>(value, spec));
			break;
		case 'o':
		case 'u':
		case 'x':
		case 'X':
			if (length == "hh") return s::f(f, static_cast<unsigned>(integer<unsigned char>(value, spec)));
			if (length == "h") return s::f(f, static_cast<unsigned>(integer<unsigned short>(value, spec)));
			if (length == "") return s::f(f, integer<unsigned>(value, spec));
			if (length == "l") return s::f(f, integer<unsigned long>(value, spec));
			if (length == "ll") return s::f(f, integer<unsigned long long>(value, spec));
			if (length == "j") return s::f(f, integer<uintmax_t>(value, spec));
			if (length == "z") return s::f(f, integer<size_t>(value, spec));
			break;
		case 'e':
		case 'E':
		case 'f':
		case 'F':
		case 'g':
		case 'G':
		case 'a':
		case 'A': {
			if (!py::isinstance<py::float_>(value) && !py::isinstance<py::int_>(value))
				throw py::type_error("%" + spec + " needs a float, not " + std::string(py::str(py::type::of(value))));
			double x = value.cast<double>();
			if (length == "" || length == "l") return s::f(f, x);
			if (length == "L") return s::f(f, static_cast<long double>(x));
			break;
		}
		case 'c':
			if (length == "") {
				if (py::isinstance<py::str>(value) && py::len(value) == 1)
					return s::f(f, static_cast<int>(value.cast<std::string>()[0] & 0xff));
				return s::f(f, static_cast<int>(integer<unsigned char>(value, spec)));
			}
			break;
		case 's':
			if (length == "") {
				std::string text = bytesOf(value, spec);
				if (text.find('\0') != std::string::npos)
					throw py::value_error("%s cannot format a string with NUL characters (C strings end there)");
				return s::f(f, text.c_str());
			}
			break;
		default:
			break;
	}
	throw py::value_error("unsupported conversion %" + spec);
}

}  // namespace

std::string formatPrintf(const char* format, py::sequence args) {
	std::string out;
	size_t next = 0;
	const size_t count = py::len(args);
	for (const char* p = format; *p;) {
		if (*p != '%') {
			out += *p++;
			continue;
		}
		const char* start = ++p;
		if (*p == '%') {
			out += '%';
			++p;
			continue;
		}
		while (*p && std::strchr("-+ #0'", *p))
			++p;
		if (*p == '*')
			throw py::value_error("* widths are not supported: write the width into the format");
		while (*p >= '0' && *p <= '9')
			++p;
		if (*p == '.') {
			++p;
			if (*p == '*')
				throw py::value_error("* precisions are not supported: write the precision into the format");
			while (*p >= '0' && *p <= '9')
				++p;
		}
		std::string length;
		if ((p[0] == 'h' && p[1] == 'h') || (p[0] == 'l' && p[1] == 'l')) {
			length.assign(p, 2);
			p += 2;
		}
		else if (*p && std::strchr("hljztL", *p)) {
			length.assign(p, 1);
			++p;
		}
		char conv = *p;
		if (!conv)
			throw py::value_error("the format ends inside a conversion");
		if (conv == 'n' || conv == 'p')
			throw py::value_error(std::string("%") + conv + " is not supported");
		++p;
		std::string spec(start, p);
		if (next >= count)
			throw py::type_error("the format needs more arguments than were given");
		out += convert(spec, length, conv, args[next++]);
	}
	if (next != count)
		throw py::type_error("the format takes " + std::to_string(next) + " argument(s), but " + std::to_string(count) +
			" were given");
	return out;
}

namespace {

std::string utf8(py::handle value) {
	return py::isinstance<py::bytes>(value) ? value.cast<std::string>() : value.cast<std::string>();
}

}  // namespace

void bindString(py::module_ m) {
	m.def("f", [](const std::string& format, py::args args) { return formatPrintf(format.c_str(), args); },
		py::arg("format"),
		"Rack's string::f (printf): each conversion goes through Rack's formatting with the C type it names "
		"(%d int, %lld long long, %u unsigned, %f double, %s str, ...). %n, %p and * are refused, and a wrong count "
		"or type raises.");
	m.def("fv", [](const std::string& format, py::sequence args) { return formatPrintf(format.c_str(), args); },
		py::arg("format"), py::arg("args"), "Rack's string::fV: as f, with the arguments as a sequence (Python's va_list).");
	m.def("lowercase", &s::lowercase, py::arg("s"));
	m.def("uppercase", &s::uppercase, py::arg("s"));
	m.def("trim", &s::trim, py::arg("s"));
	m.def("truncate", &s::truncate, py::arg("s"), py::arg("max_codepoints"));
	m.def("truncate_prefix", &s::truncatePrefix, py::arg("s"), py::arg("max_codepoints"));
	m.def("ellipsize", &s::ellipsize, py::arg("s"), py::arg("max_codepoints"));
	m.def("ellipsize_prefix", &s::ellipsizePrefix, py::arg("s"), py::arg("max_codepoints"));
	m.def("starts_with", &s::startsWith, py::arg("s"), py::arg("prefix"));
	m.def("ends_with", &s::endsWith, py::arg("s"), py::arg("suffix"));

	py::class_<s::Location>(m, "Location", "Rack's line and column (from 0) of a byte position.")
		.def(py::init([](size_t line, size_t column) { return s::Location{line, column}; }), py::arg("line") = 0,
			py::arg("column") = 0)
		.def_readwrite("line", &s::Location::line)
		.def_readwrite("column", &s::Location::column)
		.def("__eq__", [](const s::Location& a, const s::Location& b) { return a.line == b.line && a.column == b.column; })
		.def("__repr__", [](const s::Location& l) {
			return "Location(line=" + std::to_string(l.line) + ", column=" + std::to_string(l.column) + ")";
		});
	m.def("position_to_location", [](py::handle s8, size_t pos) { return s::positionToLocation(utf8(s8), pos); },
		py::arg("s"), py::arg("pos"), "Rack's: the line and column of byte position pos in the UTF-8 text.");
	m.def("location_to_position", [](py::handle s8, s::Location location) { return s::locationToPosition(utf8(s8), location); },
		py::arg("s"), py::arg("location"));

	m.def("to_base64", [](py::buffer data) {
		py::buffer_info info = data.request();
		if (info.ndim != 1 || info.itemsize != 1)
			throw py::type_error("to_base64 takes bytes (a 1-D byte buffer)");
		return s::toBase64(static_cast<const uint8_t*>(info.ptr), static_cast<size_t>(info.size));
	}, py::arg("data"), "Rack's toBase64 (both overloads): bytes to Base64 text.");
	m.def("from_base64", [](const std::string& text) {
		std::vector<uint8_t> data = s::fromBase64(text);  // Rack raises for invalid text
		return py::bytes(reinterpret_cast<const char*>(data.data()), data.size());
	}, py::arg("s"), "Rack's: Base64 text to bytes. Rack 2.6.6 skips characters that are not Base64 (its comment "
		"says it raises; it never does).");
	m.def("strcasecmp", [](const std::string& a, const std::string& b) { return s::strcasecmp(a.c_str(), b.c_str()); },
		py::arg("s1"), py::arg("s2"), "Rack's case-insensitive comparison: negative, 0 or positive.");
	py::class_<s::CaseInsensitiveCompare>(m, "CaseInsensitiveCompare", "Rack's: a less-than that ignores case.")
		.def(py::init<>())
		.def("__call__", [](const s::CaseInsensitiveCompare& c, const std::string& a, const std::string& b) { return c(a, b); },
			py::arg("a"), py::arg("b"));
	m.def("join", [](py::iterable items, const std::string& separator) {
		std::vector<std::string> parts;
		for (py::handle item : items)
			parts.push_back(item.cast<std::string>());
		return s::join(parts, separator);
	}, py::arg("container"), py::arg("seperator") = std::string(), "Rack's join over strings (its parameter is spelled seperator).");
	m.def("split", &s::split, py::arg("s"), py::arg("seperator"), py::arg("max_tokens") = 0,
		"Rack's: tokens between separators; an empty separator raises Rack's Exception.");
	m.def("format_time", [](const std::string& format, double timestamp) {
		if (!std::isfinite(timestamp) || timestamp < static_cast<double>(std::numeric_limits<time_t>::min()) ||
			timestamp > static_cast<double>(std::numeric_limits<time_t>::max()))
			throw py::value_error("the timestamp is out of range");
		time_t t = static_cast<time_t>(timestamp);
		struct tm check;
		if (!localtime_r(&t, &check))
			throw py::value_error("the timestamp has no local time");  // Rack would pass strftime a null tm
		return s::formatTime(format.c_str(), timestamp);
	}, py::arg("format"), py::arg("timestamp"), "Rack's: strftime of the local time (up to 1023 bytes).");
	m.def("format_time_iso", [](double timestamp) {
		if (!std::isfinite(timestamp))
			throw py::value_error("the timestamp is out of range");
		time_t t = static_cast<time_t>(timestamp);
		struct tm check;
		if (!localtime_r(&t, &check))
			throw py::value_error("the timestamp has no local time");
		return s::formatTimeISO(timestamp);
	}, py::arg("timestamp"));
	m.def("utf32_to_utf8", [](std::vector<uint32_t> codepoints) {
		std::u32string s32(codepoints.begin(), codepoints.end());
		std::string s8 = s::UTF32toUTF8(s32);
		return py::bytes(s8);
	}, py::arg("s32"), "Rack's: codepoints to UTF-8 bytes (codepoints above 0x10FFFF are skipped).");
	m.def("utf8_to_utf32", [](py::handle s8) {
		if (py::isinstance<py::bytes>(s8)) {
			// Rack 2.6.6's UTF8toUTF32 never ends on invalid UTF-8: it skips an invalid, overlong or surrogate
			// sequence without advancing. A str is always valid; bytes must be.
			try {
				s8.attr("decode")("utf-8");
			}
			catch (const py::error_already_set&) {
				throw py::value_error("Rack's UTF8toUTF32 never returns on invalid UTF-8 (it skips without advancing): "
					"pass valid UTF-8");
			}
		}
		std::u32string s32 = s::UTF8toUTF32(utf8(s8));
		return std::vector<uint32_t>(s32.begin(), s32.end());
	}, py::arg("s8"), "Rack's: UTF-8 (a str, or valid UTF-8 bytes) to codepoints. Rack 2.6.6 loops forever on invalid "
		"UTF-8, so invalid bytes raise ValueError.");
	m.def("utf8_next_codepoint", [](py::handle s8, size_t pos) { return s::UTF8NextCodepoint(utf8(s8), pos); },
		py::arg("s8"), py::arg("pos"), "Byte positions of the UTF-8 text (a str is encoded as UTF-8).");
	m.def("utf8_prev_codepoint", [](py::handle s8, size_t pos) { return s::UTF8PrevCodepoint(utf8(s8), pos); },
		py::arg("s8"), py::arg("pos"));
	m.def("utf8_length", [](py::handle s8) { return s::UTF8Length(utf8(s8)); }, py::arg("s8"));
	m.def("utf8_codepoint_index", [](py::handle s8, size_t pos) { return s::UTF8CodepointIndex(utf8(s8), pos); },
		py::arg("s8"), py::arg("pos"));
	m.def("utf8_codepoint_pos", [](py::handle s8, size_t index) { return s::UTF8CodepointPos(utf8(s8), index); },
		py::arg("s8"), py::arg("index"));

	py::class_<s::Version>(m, "Version", "Rack's version: dotted parts, compared part by part (integers as numbers).")
		.def(py::init<>())
		.def(py::init<const std::string&>(), py::arg("s"))
		.def_readwrite("parts", &s::Version::parts)
		.def("__str__", [](const s::Version& v) { return static_cast<std::string>(v); })
		.def("__lt__", [](s::Version& a, const s::Version& b) { return a < b; })
		.def("__eq__", [](const s::Version& a, const s::Version& b) { return a.parts == b.parts; })
		.def("get_major", &s::Version::getMajor)
		.def("get_minor", &s::Version::getMinor)
		.def("get_revision", &s::Version::getRevision)
		.def("__repr__", [](const s::Version& v) { return "Version('" + static_cast<std::string>(v) + "')"; });

	m.def("translate", [](const std::string& id) {
		runtime::requireMainThread("translating");
		return s::translate(id);
	}, py::arg("id"), "Rack's: the text for id in Rack's language, else English, else empty.");
	m.def("translate", [](const std::string& id, const std::string& language) {
		runtime::requireMainThread("translating");
		return s::translate(id, language);
	}, py::arg("id"), py::arg("language"));
	m.def("get_languages", [] {
		runtime::requireMainThread("listing languages");
		return s::getLanguages();
	});
	m.def("init", [] {
		runtime::requireMainThread("loading Rack's translations");
		s::init();
	}, "Rack's: (re)loads its translation files, as at start-up.");
}

}  // namespace services
}  // namespace bindings
}  // namespace rackpy
