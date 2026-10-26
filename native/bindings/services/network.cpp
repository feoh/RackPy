// SPDX-License-Identifier: GPL-3.0-or-later
// rack::network -> rackpy.raw.network (RP-30B). Rack's requests block in curl
// (30 s to connect, no overall limit), so they release the GIL: another
// Python thread can read a download's progress meanwhile. Python never runs
// on Rack's audio path.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <map>
#include <memory>
#include <string>

#include <rack.hpp>

#include "rackpy/runtime/json.hpp"
#include "services.hpp"

namespace rackpy {
namespace bindings {
namespace services {

namespace py = pybind11;
namespace n = rack::network;

namespace {

struct DownloadProgress {
	float value = 0.f;  // Rack's download writes it from the downloading thread
};

}  // namespace

void bindNetwork(py::module_ m) {
	py::enum_<n::Method>(m, "Method", "Rack's HTTP methods.")
		.value("METHOD_GET", n::METHOD_GET)
		.value("METHOD_POST", n::METHOD_POST)
		.value("METHOD_PUT", n::METHOD_PUT)
		.value("METHOD_DELETE", n::METHOD_DELETE)
		.export_values();
	m.attr("CookieMap") = py::module_::import("builtins").attr("dict");
	m.def("request_json", [](n::Method method, const std::string& url, py::object data, std::map<std::string, std::string> cookies)
		-> py::object {
		std::unique_ptr<json_t, decltype(&json_decref)> body(data.is_none() ? nullptr : runtime::toJson(data), json_decref);
		json_t* result;
		{
			py::gil_scoped_release release;
			result = n::requestJson(method, url, body.get(), cookies);
		}
		if (!result)
			return py::none();
		std::unique_ptr<json_t, decltype(&json_decref)> held(result, json_decref);
		return runtime::fromJson(result);
	}, py::arg("method"), py::arg("url"), py::arg("data") = py::none(), py::arg("cookies") = std::map<std::string, std::string>{},
		"Rack's: requests a JSON API (data: the query for GET, the JSON body otherwise); the parsed answer, or None on any "
		"error. Blocks, with the GIL released.");
	py::class_<DownloadProgress>(m, "DownloadProgress", "Where request_download writes its progress, 0 to 1.")
		.def(py::init<>())
		.def_property_readonly("value", [](const DownloadProgress& p) { return p.value; });
	m.def("request_download", [](const std::string& url, py::object filename, py::object progress,
		std::map<std::string, std::string> cookies) {
		std::string path = pathArg(filename);
		float* target = progress.is_none() ? nullptr : &progress.cast<DownloadProgress&>().value;
		py::gil_scoped_release release;
		return n::requestDownload(url, path, target, cookies);
	}, py::arg("url"), py::arg("filename"), py::arg("progress") = py::none(),
		py::arg("cookies") = std::map<std::string, std::string>{},
		"Rack's: downloads to filename; whether it succeeded. A DownloadProgress shows the progress while it runs (from "
		"another thread). Blocks, with the GIL released.");
	m.def("encode_url", &n::encodeUrl, py::arg("s"));
	m.def("url_path", &n::urlPath, py::arg("url"));
}

}  // namespace services
}  // namespace bindings
}  // namespace rackpy
