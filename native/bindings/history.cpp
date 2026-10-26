// SPDX-License-Identifier: GPL-3.0-or-later
// Family 9 adapter: the generated registry calls bind_history(). rack::history
// becomes rackpy.raw.history; the bindings are in native/bindings/history/.
#include "history/history.hpp"

namespace rackpy {
namespace bindings {

void bind_history(pybind11::module_ raw) {
	bindHistory(raw.def_submodule("history", "rack::history: undo actions and Rack's history (Context.history)."));
}

}  // namespace bindings
}  // namespace rackpy
