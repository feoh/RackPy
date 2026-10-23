// SPDX-License-Identifier: GPL-3.0-or-later
#include "variants.hpp"
namespace rackpy { namespace widgets {
void bindEventVariants(py::module_ mod) {
	variant<rack::widget::TransparentWidget>(mod, "TransparentWidget");
	variant<rack::widget::OpaqueWidget>(mod, "OpaqueWidget");
}
} }
