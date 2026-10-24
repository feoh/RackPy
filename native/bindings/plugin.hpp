// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <rack.hpp>

#include <string>

namespace rackpy {
namespace bindings {

/** End of plugin initialization (RP-16A): check the entry module's models
against plugin.json and add them to `plugin`, or add none. Takes the GIL.
Returns why nothing was added, or an empty string. */
std::string commitModels(rack::plugin::Plugin* plugin, const std::string& manifestPath);

/** The entry module failed: drop whatever it staged. Takes the GIL if the interpreter is alive. */
void discardModels();

}  // namespace bindings
}  // namespace rackpy
