// SPDX-License-Identifier: GPL-3.0-or-later
// `_rackpy_host`: the embedded module through which rackpy talks to its host.
//
// Internal plumbing, not Rack API. Nothing here counts toward binding coverage;
// family tasks bind Rack declarations in their own translation units.
#include <pybind11/embed.h>

#include <rack.hpp>

#include "rackpy/host_abi.h"

namespace py = pybind11;

// Registered in the inittab when the host library is loaded, before Py_Initialize.
PYBIND11_EMBEDDED_MODULE(_rackpy_host, m) {
	m.doc() = "Internal interface between rackpy and the RackPy host inside Rack. Not public API.";
	m.attr("HOST_ABI") = RACKPY_HOST_ABI;
	m.attr("BINDING") = rack::string::f("pybind11 %d.%d.%d", PYBIND11_VERSION_MAJOR, PYBIND11_VERSION_MINOR, PYBIND11_VERSION_PATCH);
	m.def("rack_version", [] { return rack::APP_VERSION; }, "The running Rack's version string.");
	m.def(
		"log_info", [](const std::string& message) { INFO("RackPy: %s", message.c_str()); },
		py::arg("message"), "Write one line to Rack's log at INFO level.");
}
