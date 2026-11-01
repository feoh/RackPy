// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <rack.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace rackpy {

/** The JSON file at a UTF-8 PATH, parsed; or null, with ERROR filled in. jansson's json_load_file opens the
path with msvcrt's fopen, which on Windows reads it in the ANSI code page, so a user folder whose path is not
ASCII fails there (RP-41); Rack's system::readFile opens it as UTF-16. */
inline json_t* loadJsonFile(const std::string& path, json_error_t* error) {
	std::vector<uint8_t> data;
	try {
		data = rack::system::readFile(path);
	}
	catch (...) {  // Rack's Exception, from libRack: caught whatever its type_info (RP-40)
		std::snprintf(error->text, sizeof(error->text), "unable to open %s", path.c_str());
		error->source[0] = '\0';
		error->line = error->column = -1;
		error->position = 0;
		return nullptr;
	}
	return json_loadb(data.empty() ? "" : reinterpret_cast<const char*>(data.data()), data.size(), 0, error);
}

}  // namespace rackpy
