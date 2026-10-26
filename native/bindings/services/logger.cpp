// SPDX-License-Identifier: GPL-3.0-or-later
// rack::logger -> rackpy.raw.logger (RP-30A). Rack's log is thread-safe (it
// takes its own lock and writes the file), so the call releases the GIL. The
// message is formatted first through Rack's string::f (formatPrintf) and
// passed to Rack's log as one %s argument: the same text, and a Python format
// string can never reach Rack's printf.
#include <pybind11/pybind11.h>

#include <string>

#include <rack.hpp>

#include "rackpy/runtime/threads.hpp"
#include "services.hpp"

namespace rackpy {
namespace bindings {
namespace services {

namespace py = pybind11;
namespace l = rack::logger;

void bindLogger(py::module_ m) {
	py::enum_<l::Level>(m, "Level", "Rack's log levels.")
		.value("DEBUG_LEVEL", l::DEBUG_LEVEL)
		.value("INFO_LEVEL", l::INFO_LEVEL)
		.value("WARN_LEVEL", l::WARN_LEVEL)
		.value("FATAL_LEVEL", l::FATAL_LEVEL)
		.export_values();
	m.def("log", [](l::Level level, const std::string& filename, int line, const std::string& func,
		const std::string& format, py::args args) {
		std::string message = formatPrintf(format.c_str(), args);
		py::gil_scoped_release release;  // Rack's lock and file write
		l::log(level, filename.c_str(), line, func.c_str(), "%s", message.c_str());
	}, py::arg("level"), py::arg("filename"), py::arg("line"), py::arg("func"), py::arg("format"),
		"Rack's log (its macros DEBUG/INFO/WARN/FATAL call it): the format and arguments as rackpy.raw.string.f, "
		"written with the level and source location. Thread-safe.");
	m.def("get_log_path", [] {
		runtime::requireMainThread("reading Rack's log path");
		return l::logPath;
	}, "Rack's logPath: the log file Rack opened at start-up (empty when it logs to the terminal).");
	m.def("set_log_path", [](const std::string& path) {
		runtime::requireMainThread("setting Rack's log path");
		l::logPath = path;
	}, py::arg("path"), "Sets Rack's logPath variable. Rack opened its log at start-up: later messages still go there.");
}

}  // namespace services
}  // namespace bindings
}  // namespace rackpy
