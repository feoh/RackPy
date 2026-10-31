// SPDX-License-Identifier: GPL-3.0-or-later
#include "loader_config.hpp"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <link.h>
#endif

#include <cctype>

#include "rackpy/host_abi.h"

namespace rackpy {
namespace loader {

bool isContainedPath(const std::string& rel) {
	if (rel.empty() || rel[0] == '/' || rel[0] == '\\')
		return false;
	for (const std::string& part : rack::string::split(rel, "/")) {
		if (part == "..")
			return false;
	}
	return rel.find('\\') == std::string::npos;
}

bool isModuleName(const std::string& name) {
	if (name.empty())
		return false;
	for (const std::string& part : rack::string::split(name, ".")) {
		if (part.empty() || std::isdigit((unsigned char) part[0]))
			return false;
		for (char c : part) {
			if (!(std::isalnum((unsigned char) c) || c == '_'))
				return false;
		}
	}
	return true;
}

bool parseConfig(json_t* rootJ, Config& cfg, std::string& err) {
	if (!json_is_object(rootJ)) {
		err = "the loader configuration must be a JSON object";
		return false;
	}
	json_t* abiJ = json_object_get(rootJ, "abi");
	if (!json_is_integer(abiJ) || json_integer_value(abiJ) != RACKPY_HOST_ABI) {
		err = rack::string::f("\"abi\" must be %d, the host ABI this loader was built for", RACKPY_HOST_ABI);
		return false;
	}
	cfg.abi = (int) json_integer_value(abiJ);

	struct Field {
		const char* key;
		std::string* out;
	};
	const Field paths[] = {
		{"host", &cfg.host},
		{"libpython", &cfg.libpython},
		{"stdlib_marker", &cfg.stdlibMarker},
		{"python_path", &cfg.pythonPath},
	};
	for (const Field& f : paths) {
		json_t* j = json_object_get(rootJ, f.key);
		if (!json_is_string(j) || !isContainedPath(json_string_value(j))) {
			err = rack::string::f("\"%s\" must be a relative path inside the plugin, without \"..\"", f.key);
			return false;
		}
		*f.out = json_string_value(j);
	}

	json_t* entryJ = json_object_get(rootJ, "entry");
	if (entryJ) {
		if (!json_is_string(entryJ)) {
			err = "\"entry\" must be a string";
			return false;
		}
		cfg.entry = json_string_value(entryJ);
		if (!cfg.entry.empty() && !isModuleName(cfg.entry)) {
			err = rack::string::f("\"entry\" is not a Python module name: %s", cfg.entry.c_str());
			return false;
		}
	}
	return true;
}

bool readConfig(const std::string& path, Config& cfg, std::string& err) {
	json_error_t error;
	json_t* rootJ = json_load_file(path.c_str(), 0, &error);
	if (!rootJ) {
		err = rack::string::f("cannot read %s: %s", path.c_str(), error.text);
		return false;
	}
	DEFER({json_decref(rootJ);});
	if (!parseConfig(rootJ, cfg, err)) {
		err = path + ": " + err;
		return false;
	}
	return true;
}

namespace {

// A loaded image that is a CPython runtime: libpython*, or on macOS a framework build's
// Python.framework/Versions/X.Y/Python.
bool isPython(const std::string& name) {
	return rack::string::startsWith(rack::system::getFilename(name), "libpython")
		|| name.find("/Python.framework/") != std::string::npos;
}

#if !defined(__APPLE__)
int collectPython(struct dl_phdr_info* info, size_t, void* data) {
	std::string name = info->dlpi_name ? info->dlpi_name : "";
	if (isPython(name))
		static_cast<std::vector<std::string>*>(data)->push_back(name);
	return 0;
}
#endif

}  // namespace

std::vector<std::string> loadedPythons() {
	std::vector<std::string> names;
#if defined(__APPLE__)
	// dyld's image list, as dl_iterate_phdr's on Linux: also images other plugins opened with RTLD_LOCAL.
	for (uint32_t i = 0, n = _dyld_image_count(); i < n; i++) {
		const char* name = _dyld_get_image_name(i);
		if (name && isPython(name))
			names.push_back(name);
	}
#else
	dl_iterate_phdr(collectPython, &names);
#endif
	return names;
}

}  // namespace loader
}  // namespace rackpy
