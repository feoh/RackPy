// SPDX-License-Identifier: GPL-3.0-or-later
// Family 8 adapter: the generated registry calls bind_services(). The bindings
// are in native/bindings/services/ (string, random and logger are family 1).
#include "services/services.hpp"

namespace rackpy {
namespace bindings {

void bind_services(pybind11::module_ raw) {
	services::bindSystem(raw.def_submodule("system", "rack::system: files, paths, archives, time and processes."));
	services::bindNetwork(raw.def_submodule("network", "rack::network: HTTP requests, downloads and URLs."));
	services::bindSettings(raw.def_submodule("settings", "rack::settings: Rack's settings."));
}

}  // namespace bindings
}  // namespace rackpy
