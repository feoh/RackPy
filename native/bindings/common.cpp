// SPDX-License-Identifier: GPL-3.0-or-later
// Family 1 adapter (RP-12): the generated registry calls bind_common().
// The bindings themselves are in native/bindings/values/.
#include "values/values.hpp"

namespace rackpy {
namespace bindings {

void bind_common(pybind11::module_ raw) {
	values::bindNanovgTypes(raw);
	values::bindMath(raw);
	values::bindColor(raw);
	values::bindCommon(raw);
	values::bindQuantity(raw);
}

}  // namespace bindings
}  // namespace rackpy
