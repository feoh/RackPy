// SPDX-License-Identifier: GPL-3.0-or-later
// rack::componentlibrary's concrete widgets -> rackpy.raw.componentlibrary
// (RP-28C). Their own C++ constructors run, so they load Rack's installed
// artwork (asset::system); RackPy redistributes none of it. The library's
// CRTP templates (RedLight, MediumLight<TBase>, LightButton<TBase, TLight>...)
// are header code that composes classes, so they are Python specializations
// (src/rackpy/raw/_componentlibrary.py) over these and rackpy.raw.app. The
// classes are bound in components_*.cpp, split only to compile in parallel.
#include "components.hpp"

namespace rackpy {
namespace widgets {
namespace appdetail {

void bindComponentLibrary(py::module_ raw) {
	py::module_ mod = raw.def_submodule("componentlibrary",
		"rack::componentlibrary: Rack's components, which draw Rack's installed artwork (RP-28C).");
	mod.attr("SCHEME_BLACK_TRANSPARENT") = cl::SCHEME_BLACK_TRANSPARENT;
	mod.attr("SCHEME_BLACK") = cl::SCHEME_BLACK;
	mod.attr("SCHEME_WHITE") = cl::SCHEME_WHITE;
	mod.attr("SCHEME_RED") = cl::SCHEME_RED;
	mod.attr("SCHEME_ORANGE") = cl::SCHEME_ORANGE;
	mod.attr("SCHEME_YELLOW") = cl::SCHEME_YELLOW;
	mod.attr("SCHEME_GREEN") = cl::SCHEME_GREEN;
	mod.attr("SCHEME_CYAN") = cl::SCHEME_CYAN;
	mod.attr("SCHEME_BLUE") = cl::SCHEME_BLUE;
	mod.attr("SCHEME_PURPLE") = cl::SCHEME_PURPLE;
	mod.attr("SCHEME_LIGHT_GRAY") = cl::SCHEME_LIGHT_GRAY;
	mod.attr("SCHEME_DARK_GRAY") = cl::SCHEME_DARK_GRAY;
	bindComponentKnobs(mod);  // Rogan's variants need Rogan, from here
	bindComponentRogans(mod);
	bindComponentRogans2(mod);
	bindComponentSwitches(mod);
	bindComponentPorts(mod);
	// Rack's plain aliases.
	mod.attr("LEDSlider") = mod.attr("VCVSlider");
	mod.attr("LEDButton") = mod.attr("VCVButton");
	mod.attr("LEDBezel") = mod.attr("VCVBezel");
}

}  // namespace appdetail
}  // namespace widgets
}  // namespace rackpy
