// SPDX-License-Identifier: GPL-3.0-or-later
// Rack-native file chooser convenience API. osdialog is bundled with the Rack SDK
// but is not part of the rack.hpp API inventory; keep it behind the rackpy.dialog facade.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <rack.hpp>
#include <osdialog.h>  // Owner-approved isolated exception: Rack-bundled UI utility, outside rack.hpp.

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/threads.hpp"
#include "services.hpp"

namespace rackpy {
namespace bindings {
namespace services {

namespace py = pybind11;

namespace {

std::optional<std::string> chooseFile(const std::string& action, py::object directory, py::object filename,
	py::object filters) {
	runtime::requireMainThread("opening a Rack file dialog");
	auto token = runtime::binding().current();
	if (!token)
		throw runtime::WrongContext("a Rack file dialog needs Rack's live bound UI context");
	if (rack::contextGet() != token->context)
		throw runtime::WrongContext("a Rack file dialog needs the bound Rack UI context active on this thread");
	runtime::checkContext(*token);

	osdialog_file_action nativeAction;
	if (action == "open")
		nativeAction = OSDIALOG_OPEN;
	else if (action == "open_dir")
		nativeAction = OSDIALOG_OPEN_DIR;
	else if (action == "save")
		nativeAction = OSDIALOG_SAVE;
	else
		throw py::value_error("action must be 'open', 'open_dir', or 'save'");

	if (nativeAction != OSDIALOG_SAVE && !filename.is_none())
		throw py::value_error("filename is only supported by a save dialog");

	const std::string startPath = directory.is_none() ? std::string() : pathArg(directory);
	const std::string startName = filename.is_none() ? std::string() : filename.cast<std::string>();
	const std::string filterText = filters.is_none() ? std::string() : filters.cast<std::string>();
	std::unique_ptr<osdialog_filters, decltype(&osdialog_filters_free)> parsedFilters(
		filterText.empty() ? nullptr : osdialog_filters_parse(filterText.c_str()), &osdialog_filters_free);
	std::unique_ptr<char, decltype(&std::free)> selected(
		osdialog_file(nativeAction, startPath.empty() ? nullptr : startPath.c_str(),
			startName.empty() ? nullptr : startName.c_str(), parsedFilters.get()),
		&std::free);
	if (!selected)
		return std::nullopt;
	return std::string(selected.get());
}

}  // namespace

void bindDialog(py::module_ dialog) {
	dialog.def("choose_file", &chooseFile,
		py::arg("action"), py::arg("directory") = py::none(), py::arg("filename") = py::none(),
		py::arg("filters") = py::none(),
		"Open Rack's native file dialog on the main thread. Returns a selected path, or None on cancel. "
		"Filters use osdialog's `Name:ext,ext;Other:ext` syntax.");
}

}  // namespace services
}  // namespace bindings
}  // namespace rackpy
