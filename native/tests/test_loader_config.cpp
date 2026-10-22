// SPDX-License-Identifier: GPL-3.0-or-later
// Native unit tests for the loader's configuration checks. Runs outside Rack,
// linked against the SDK's libRack for jansson and rack::string; this proves
// nothing about loading in a Rack host (tests/integration does that).
#include <rack.hpp>

#include <cstdio>
#include <functional>
#include <string>

#include "loader_config.hpp"
#include "rackpy/host_abi.h"

using rackpy::loader::Config;

namespace {

int failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

json_t* parse(const std::string& text) {
	json_error_t error;
	json_t* j = json_loads(text.c_str(), 0, &error);
	if (!j)
		std::fprintf(stderr, "bad test JSON: %s\n", error.text);
	return j;
}

std::string valid(const std::string& extra = "") {
	return rack::string::f(
		"{\"abi\": %d, \"host\": \"rackpy_host.so\", \"libpython\": \"lib/libpython3.14.so.1.0\", "
		"\"stdlib_marker\": \"lib/python3.14/os.py\", \"python_path\": \"pythonpath\"%s}",
		RACKPY_HOST_ABI, extra.c_str());
}

bool parses(const std::string& text, Config& cfg, std::string& err) {
	json_t* j = parse(text);
	bool ok = rackpy::loader::parseConfig(j, cfg, err);
	json_decref(j);
	return ok;
}

void testValidConfig() {
	Config cfg;
	std::string err;
	CHECK(parses(valid(", \"entry\": \"my_plugin.main\""), cfg, err));
	CHECK(err.empty());
	CHECK(cfg.abi == RACKPY_HOST_ABI);
	CHECK(cfg.host == "rackpy_host.so");
	CHECK(cfg.libpython == "lib/libpython3.14.so.1.0");
	CHECK(cfg.stdlibMarker == "lib/python3.14/os.py");
	CHECK(cfg.pythonPath == "pythonpath");
	CHECK(cfg.entry == "my_plugin.main");
}

void testEntryIsOptional() {
	Config cfg;
	std::string err;
	CHECK(parses(valid(), cfg, err));
	CHECK(cfg.entry.empty());
	CHECK(parses(valid(", \"entry\": \"\""), cfg, err));
}

void testRejections() {
	struct Case {
		std::string text;
		std::string expect;  // substring of the error
	};
	const Case cases[] = {
		{"[]", "JSON object"},
		{"{\"abi\": 999}", "\"abi\" must be"},
		{rack::string::f("{\"abi\": %d}", RACKPY_HOST_ABI), "\"host\""},
		{valid(", \"host\": \"../escape.so\""), "\"host\""},
		{valid(", \"host\": \"/abs/host.so\""), "\"host\""},
		{valid(", \"python_path\": \"a\\\\b\""), "\"python_path\""},
		{valid(", \"entry\": 3"), "\"entry\" must be a string"},
		{valid(", \"entry\": \"not-a-module\""), "not a Python module name"},
		{valid(", \"entry\": \"pkg..mod\""), "not a Python module name"},
		{valid(", \"entry\": \"9lives\""), "not a Python module name"},
	};
	for (const Case& c : cases) {
		Config cfg;
		std::string err;
		bool ok = parses(c.text, cfg, err);
		CHECK(!ok);
		if (err.find(c.expect) == std::string::npos) {
			std::fprintf(stderr, "for %s: error \"%s\" lacks \"%s\"\n", c.text.c_str(), err.c_str(), c.expect.c_str());
			failures++;
		}
	}
}

void testContainedPaths() {
	using rackpy::loader::isContainedPath;
	CHECK(isContainedPath("python/lib/libpython3.14.so.1.0"));
	CHECK(isContainedPath("a..b/c"));
	CHECK(!isContainedPath(""));
	CHECK(!isContainedPath(".."));
	CHECK(!isContainedPath("a/../b"));
	CHECK(!isContainedPath("/etc/passwd"));
}

void testReadConfigReportsPath() {
	Config cfg;
	std::string err;
	CHECK(!rackpy::loader::readConfig("/nonexistent/rackpy-loader.json", cfg, err));
	CHECK(err.find("/nonexistent/rackpy-loader.json") != std::string::npos);
}

void testNoPythonLoaded() {
	// This executable does not link libpython, so the collision check must see none.
	CHECK(rackpy::loader::loadedPythons().empty());
}

}  // namespace

int main() {
	const std::function<void()> tests[] = {
		testValidConfig, testEntryIsOptional, testRejections, testContainedPaths, testReadConfigReportsPath,
		testNoPythonLoaded};
	for (const auto& t : tests)
		t();
	if (failures) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("loader config tests passed\n");
	return 0;
}
