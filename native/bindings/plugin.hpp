// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

#include <rack.hpp>

#include <string>

namespace rackpy {
namespace bindings {

/** End of plugin initialization (RP-16A): check the entry module's models
against plugin.json and add them to `plugin`, or add none. Takes the GIL.
Returns why nothing was added, or an empty string. */
std::string commitModels(rack::plugin::Plugin* plugin, const std::string& manifestPath);

/** The Python handle for a Rack model (rackpy.raw.plugin.Model), or None. Main thread, GIL. */
pybind11::object modelObject(rack::plugin::Model* model);
/** The live Rack model of a rackpy.raw.plugin.Model handle. Main thread, GIL. */
rack::plugin::Model* modelArgument(pybind11::handle value);

/** Rack's settingsToJson/settingsFromJson for this plugin (RP-15D): the
Python settings handlers, contained. A new reference or null (Rack then keeps
the stored settings); the input is borrowed. Never throw. */
json_t* settingsToJson() noexcept;
void settingsFromJson(json_t* json) noexcept;

/** The entry module failed: drop whatever it staged. Takes the GIL if the interpreter is alive. */
void discardModels();

}  // namespace bindings
}  // namespace rackpy
