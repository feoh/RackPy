// SPDX-License-Identifier: GPL-3.0-or-later
// Family 0 adapter: rack.hpp's own declarations. Its one counted record is the
// PRIVATE macro, which marks Rack's internal API; it has no run-time meaning,
// and RackPy never calls what it marks (docs/coverage.md). Nothing to bind.
#include <pybind11/pybind11.h>

namespace rackpy {
namespace bindings {

void bind_entry_point(pybind11::module_ raw) {
	(void) raw;
}

}  // namespace bindings
}  // namespace rackpy
