// SPDX-License-Identifier: GPL-3.0-or-later
// Family 2 adapter: the generated registry calls bind_engine(). rack::engine
// becomes rackpy.raw.engine; the bindings are in native/bindings/engine/.
#include "engine/components.hpp"

namespace rackpy {
namespace bindings {

void bind_engine(pybind11::module_ raw) {
	pybind11::module_ engine = raw.def_submodule("engine", "rack::engine: modules, ports, parameters, lights, cables and the engine.");
	bindComponents(engine);
}

}  // namespace bindings
}  // namespace rackpy
