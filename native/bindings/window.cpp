// SPDX-License-Identifier: GPL-3.0-or-later
// Family 5 adapter: the generated registry calls bind_window(). The only bound
// slice of rack::window so far is rackpy.raw.window.Svg, which SvgWidget needs
// while family 4 is bound, so bind_widgets() binds it (the registry calls
// family 4 first). The rest of rack::window is RP-27.
#include <pybind11/pybind11.h>

namespace rackpy {
namespace bindings {

void bind_window(pybind11::module_ raw) {
	(void) raw;
}

}  // namespace bindings
}  // namespace rackpy
