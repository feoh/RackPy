// SPDX-License-Identifier: GPL-3.0-or-later
// Loader configuration and pre-flight checks, kept apart from the plugin entry
// points so native unit tests can exercise them without a Rack host.
#pragma once
#include <rack.hpp>

#include <string>
#include <vector>

namespace rackpy {
namespace loader {

/** Contents of rackpy-loader.json, shipped next to plugin.so. Paths are relative. */
struct Config {
	int abi = 0;
	std::string host;  // host library, relative to the plugin directory
	std::string libpython;  // relative to the bundled runtime directory
	std::string stdlibMarker;  // a file that must exist in the runtime, e.g. lib/python3.14/os.py
	std::string pythonPath;  // relative to the plugin directory
	std::string entry;  // trusted module name, or empty
};

/** True if `rel` is a non-empty relative path with no `..` component. */
bool isContainedPath(const std::string& rel);

/** True if `name` is a dotted Python module name, e.g. `my_plugin.main`. */
bool isModuleName(const std::string& name);

/** Fills `cfg` from a parsed manifest. Returns false and sets `err` on any invalid field. */
bool parseConfig(json_t* rootJ, Config& cfg, std::string& err);

bool readConfig(const std::string& path, Config& cfg, std::string& err);

/** File names of every libpython mapped into this process, including RTLD_LOCAL ones. */
std::vector<std::string> loadedPythons();

}  // namespace loader
}  // namespace rackpy
