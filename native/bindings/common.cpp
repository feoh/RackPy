// SPDX-License-Identifier: GPL-3.0-or-later
// Family 1 adapter (RP-12): the generated registry calls bind_common().
// The bindings themselves are in native/bindings/values/.
#include "services/services.hpp"
#include "values/values.hpp"

namespace rackpy {
namespace bindings {

void bind_common(pybind11::module_ raw) {
	values::bindNanovgTypes(raw);
	values::bindMath(raw);
	values::bindColor(raw);
	values::bindCommon(raw);
	values::bindQuantity(raw);
	services::bindString(raw.def_submodule("string", "rack::string: text, UTF-8, Base64, versions and translations."));
	services::bindRandom(raw.def_submodule("random", "rack::random: Rack's random number generator."));
	services::bindLogger(raw.def_submodule("logger", "rack::logger: Rack's log."));
	services::bindMutex(raw);
}

}  // namespace bindings
}  // namespace rackpy
