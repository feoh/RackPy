// SPDX-License-Identifier: GPL-3.0-or-later
// `_rackpy_host`: the embedded module through which rackpy talks to its host.
//
// Internal plumbing, not Rack API: nothing here counts toward binding coverage.
// rackpy.runtime and rackpy._bootstrap wrap it; plugin code should not import it.
#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <rack.hpp>

#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/diagnostics.hpp"
#include "rackpy/runtime/python.hpp"
#include "rackpy/host_abi.h"
#include "rackpy/runtime/threads.hpp"

#ifdef RACKPY_TEST_HOOKS
#include "testing.hpp"
#endif

namespace py = pybind11;
namespace rt = rackpy::runtime;

// Registered in the inittab when the host library is loaded, before Py_Initialize.
PYBIND11_EMBEDDED_MODULE(_rackpy_host, m) {
	m.doc() = "Internal interface between rackpy and the RackPy host inside Rack. Not public API.";
	m.attr("HOST_ABI") = RACKPY_HOST_ABI;
	m.attr("BINDING") = rack::string::f("pybind11 %d.%d.%d", PYBIND11_VERSION_MAJOR, PYBIND11_VERSION_MINOR, PYBIND11_VERSION_PATCH);
	m.attr("DIAGNOSTIC_CAPACITY") = rt::DIAGNOSTIC_CAPACITY;
#ifdef RACKPY_TEST_HOOKS
	m.attr("TEST_HOOKS") = true;
#else
	m.attr("TEST_HOOKS") = false;
#endif
	m.def("rack_version", [] { return rack::APP_VERSION; }, "The running Rack's version string.");
	m.def(
		"log_info", [](const std::string& message) { INFO("RackPy: %s", message.c_str()); },
		py::arg("message"), "Write one line to Rack's log at INFO level.");
	m.def(
		"log_warning", [](const std::string& message) { WARN("RackPy: %s", message.c_str()); },
		py::arg("message"), "Write one line to Rack's log at WARN level.");
	m.def("thread_role", [] { return std::string(rt::roleName(rt::threadRole())); });
	m.def("bound_context_generation", []() -> py::object {
		auto token = rt::binding().current();
		return token ? py::object(py::int_(token->generation)) : py::object(py::none());
	});
	m.def(
		"diagnostics",
		[](uint64_t after) {
			py::list out;
			for (const rt::Diagnostic& d : rt::readDiagnostics(after))
				out.append(py::make_tuple(d.sequence, d.time, rt::codeName(d.code), rt::roleName(d.role), d.message));
			return out;
		},
		py::arg("after") = 0);
	m.def("diagnostics_recorded", &rt::diagnosticsRecorded);
	m.def("dispatch_stats", [] {
		rt::DispatchStats s = rt::dispatchStats();
		return py::dict(py::arg("calls") = s.calls, py::arg("refused_rack_thread") = s.refusedRackThread,
			py::arg("raised") = s.raised);
	});
	m.def("native_references", &rt::nativeReferences);
	m.def("transferred_objects", &rt::transferredObjects);
	m.def("pending_releases", &rt::pendingReleases);
	m.def("identity_entries", &rt::identityEntries);
#ifdef RACKPY_TEST_HOOKS
	rackpy::testing::define(m.def_submodule("_testing", "Test hooks; only in RACKPY_TEST_HOOKS builds."));
#endif
}
