// SPDX-License-Identifier: GPL-3.0-or-later
// Family 4 adapter: the generated registry calls bind_widgets(). rack::widget
// becomes rackpy.raw.widget; the bindings are in native/bindings/widget/. They
// also bind the slice of rack::window that SvgWidget needs (rackpy.raw.window.Svg).
#include "widget/tree.hpp"

namespace rackpy {
namespace bindings {

void bind_widgets(pybind11::module_ raw) {
	bindWidgetTrees(raw);
}

}  // namespace bindings
}  // namespace rackpy
