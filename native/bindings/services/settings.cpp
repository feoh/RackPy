// SPDX-License-Identifier: GPL-3.0-or-later
// rack::settings -> rackpy.raw.settings (RP-30C). Rack's settings are plain
// globals that its UI reads and writes on the main thread (and its engine
// reads some, such as threadCount, unlocked, as Rack's own menus leave them):
// so RackPy reads and writes them on the main thread only, one property per
// setting of the `settings` object. Rack saves them when it quits.
//
// Two are read-only: Rack sets `headless` and `isPlugin` at start-up and its
// code assumes them afterwards (a window exists exactly when not headless).
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cmath>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <rack.hpp>

#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/json.hpp"
#include "rackpy/runtime/threads.hpp"
#include "services.hpp"

namespace rackpy {
namespace bindings {
namespace services {

namespace py = pybind11;
namespace st = rack::settings;

namespace {

struct Settings {};

/** A ModuleInfo in Rack's moduleInfos, found again by its slugs at every use. */
struct ModuleInfoView {
	std::string plugin, module;
	st::ModuleInfo& get() const {
		runtime::requireMainThread("using Rack's settings");
		st::ModuleInfo* info = st::getModuleInfo(plugin, module);
		if (!info)
			throw runtime::StaleHandle("Rack's settings have no module info for " + plugin + " " + module + " now");
		return *info;
	}
};

void requireMain(const char* what) {
	runtime::requireMainThread(what);
}

float finite(float v, const char* name) {
	if (!std::isfinite(v))
		throw py::value_error(std::string(name) + " must be finite");
	return v;
}

}  // namespace

void bindSettings(py::module_ m) {
	py::enum_<st::KnobMode>(m, "KnobMode", "Rack's knob dragging modes.")
		.value("KNOB_MODE_LINEAR", st::KNOB_MODE_LINEAR)
		.value("KNOB_MODE_SCALED_LINEAR", st::KNOB_MODE_SCALED_LINEAR)
		.value("KNOB_MODE_ROTARY_ABSOLUTE", st::KNOB_MODE_ROTARY_ABSOLUTE)
		.value("KNOB_MODE_ROTARY_RELATIVE", st::KNOB_MODE_ROTARY_RELATIVE)
		.export_values();
	py::enum_<st::BrowserSort>(m, "BrowserSort", "Rack's module browser orders.")
		.value("BROWSER_SORT_UPDATED", st::BROWSER_SORT_UPDATED)
		.value("BROWSER_SORT_LAST_USED", st::BROWSER_SORT_LAST_USED)
		.value("BROWSER_SORT_MOST_USED", st::BROWSER_SORT_MOST_USED)
		.value("BROWSER_SORT_BRAND", st::BROWSER_SORT_BRAND)
		.value("BROWSER_SORT_NAME", st::BROWSER_SORT_NAME)
		.value("BROWSER_SORT_RANDOM", st::BROWSER_SORT_RANDOM)
		.export_values();

	py::class_<st::ModuleInfo>(m, "ModuleInfo", "Rack's per-module record: enabled, favorite, times added, last added.")
		.def(py::init<>())
		.def_readwrite("enabled", &st::ModuleInfo::enabled)
		.def_readwrite("favorite", &st::ModuleInfo::favorite)
		.def_readwrite("added", &st::ModuleInfo::added)
		.def_readwrite("last_added", &st::ModuleInfo::lastAdded, "A Unix time, or NaN.");
	py::class_<ModuleInfoView>(m, "ModuleInfoView", py::is_final(),
		"A ModuleInfo in Rack's settings (get_module_info), found again by its slugs: writes change Rack's.")
		.def_property("enabled", [](const ModuleInfoView& v) { return v.get().enabled; },
			[](const ModuleInfoView& v, bool x) { v.get().enabled = x; })
		.def_property("favorite", [](const ModuleInfoView& v) { return v.get().favorite; },
			[](const ModuleInfoView& v, bool x) { v.get().favorite = x; })
		.def_property("added", [](const ModuleInfoView& v) { return v.get().added; },
			[](const ModuleInfoView& v, int x) { v.get().added = x; })
		.def_property("last_added", [](const ModuleInfoView& v) { return v.get().lastAdded; },
			[](const ModuleInfoView& v, double x) { v.get().lastAdded = x; });
	py::class_<st::PluginWhitelist>(m, "PluginWhitelist",
		"Rack's subscription record for one plugin: all of it (subscribed), or some modules.")
		.def(py::init<>())
		.def_readwrite("subscribed", &st::PluginWhitelist::subscribed)
		.def_readwrite("module_slugs", &st::PluginWhitelist::moduleSlugs, "A set copy; assign a set.");

	py::class_<Settings> cls(m, "Settings", py::is_final(),
		"Rack's settings, one property each (the module's `settings` object). Main thread; Rack saves them as it quits.");
#define RACKPY_SETTING(python, field, T) \
	cls.def_property(#python, [](const Settings&) -> T { \
		requireMain("reading Rack's settings"); \
		return st::field; \
	}, [](const Settings&, T value) { \
		requireMain("changing Rack's settings"); \
		st::field = value; \
	});
#define RACKPY_FLOAT_SETTING(python, field) \
	cls.def_property(#python, [](const Settings&) { \
		requireMain("reading Rack's settings"); \
		return st::field; \
	}, [](const Settings&, float value) { \
		requireMain("changing Rack's settings"); \
		st::field = finite(value, #python); \
	});
#define RACKPY_READONLY_SETTING(python, field) \
	cls.def_property_readonly(#python, [](const Settings&) { \
		requireMain("reading Rack's settings"); \
		return st::field; \
	}, "Read-only: Rack sets it at start-up and its code assumes it afterwards.");
	RACKPY_SETTING(settings_path, settingsPath, std::string)
	RACKPY_SETTING(dev_mode, devMode, bool)
	RACKPY_READONLY_SETTING(headless, headless)
	RACKPY_READONLY_SETTING(is_plugin, isPlugin)
	RACKPY_SETTING(restart, restart, bool)
	RACKPY_SETTING(language, language, std::string)
	RACKPY_SETTING(safe_mode, safeMode, bool)
	RACKPY_SETTING(token, token, std::string)
	RACKPY_SETTING(window_maximized, windowMaximized, bool)
	RACKPY_SETTING(window_size, windowSize, rack::math::Vec)
	RACKPY_SETTING(window_pos, windowPos, rack::math::Vec)
	RACKPY_SETTING(invert_zoom, invertZoom, bool)
	RACKPY_SETTING(mouse_wheel_zoom, mouseWheelZoom, bool)
	RACKPY_FLOAT_SETTING(pixel_ratio, pixelRatio)
	RACKPY_SETTING(ui_theme, uiTheme, std::string)
	RACKPY_FLOAT_SETTING(cable_opacity, cableOpacity)
	RACKPY_FLOAT_SETTING(cable_tension, cableTension)
	RACKPY_FLOAT_SETTING(rack_brightness, rackBrightness)
	RACKPY_FLOAT_SETTING(spotlight_brightness, spotlightBrightness)
	RACKPY_FLOAT_SETTING(spotlight_radius, spotlightRadius)
	RACKPY_FLOAT_SETTING(halo_brightness, haloBrightness)
	RACKPY_SETTING(allow_cursor_lock, allowCursorLock, bool)
	RACKPY_SETTING(knob_mode, knobMode, st::KnobMode)
	RACKPY_SETTING(knob_scroll, knobScroll, bool)
	RACKPY_FLOAT_SETTING(knob_linear_sensitivity, knobLinearSensitivity)
	RACKPY_FLOAT_SETTING(knob_scroll_sensitivity, knobScrollSensitivity)
	RACKPY_FLOAT_SETTING(sample_rate, sampleRate)
	RACKPY_SETTING(tooltips, tooltips, bool)
	RACKPY_SETTING(cpu_meter, cpuMeter, bool)
	RACKPY_SETTING(lock_modules, lockModules, bool)
	RACKPY_SETTING(squeeze_modules, squeezeModules, bool)
	RACKPY_SETTING(prefer_dark_panels, preferDarkPanels, bool)
	RACKPY_FLOAT_SETTING(frame_rate_limit, frameRateLimit)
	RACKPY_FLOAT_SETTING(autosave_interval, autosaveInterval)
	RACKPY_SETTING(skip_load_on_launch, skipLoadOnLaunch, bool)
	RACKPY_SETTING(last_patch_directory, lastPatchDirectory, std::string)
	RACKPY_SETTING(last_selection_directory, lastSelectionDirectory, std::string)
	RACKPY_SETTING(recent_patch_paths, recentPatchPaths, std::list<std::string>)
	RACKPY_SETTING(cable_colors, cableColors, std::vector<NVGcolor>)
	RACKPY_SETTING(cable_labels, cableLabels, std::vector<std::string>)
	RACKPY_SETTING(cable_auto_rotate, cableAutoRotate, bool)
	RACKPY_SETTING(auto_check_updates, autoCheckUpdates, bool)
	RACKPY_SETTING(verify_https_certs, verifyHttpsCerts, bool)
	RACKPY_SETTING(show_tips_on_launch, showTipsOnLaunch, bool)
	RACKPY_SETTING(tip_index, tipIndex, int)
	RACKPY_SETTING(browser_sort, browserSort, st::BrowserSort)
	RACKPY_FLOAT_SETTING(browser_zoom, browserZoom)
#undef RACKPY_SETTING
#undef RACKPY_FLOAT_SETTING
#undef RACKPY_READONLY_SETTING
	cls.def_property("thread_count", [](const Settings&) {
		requireMain("reading Rack's settings");
		return st::threadCount;
	}, [](const Settings&, int n) {
		requireMain("changing Rack's settings");
		if (n < 1)
			throw py::value_error("Rack's engine needs at least one thread");
		st::threadCount = n;  // Rack's engine relaunches its workers at its next block
	}, "Rack's engine threads: its engine reads it at each block.");
	cls.def_property("plugin_settings", [](const Settings&) -> py::object {
		requireMain("reading Rack's settings");
		return st::pluginSettingsJ ? runtime::fromJson(st::pluginSettingsJ) : py::none();
	}, [](const Settings&, py::handle data) {
		requireMain("changing Rack's settings");
		json_t* json = data.is_none() ? nullptr : runtime::toJson(data);
		json_decref(st::pluginSettingsJ);
		st::pluginSettingsJ = json;
	}, "Rack's pluginSettingsJ: every plugin's saved settings, as Python data (a copy), or None.");
	cls.def_property("module_infos", [](const Settings&) {
		requireMain("reading Rack's settings");
		return st::moduleInfos;
	}, [](const Settings&, std::map<std::string, std::map<std::string, st::ModuleInfo>> infos) {
		requireMain("changing Rack's settings");
		st::moduleInfos = std::move(infos);
	}, "Rack's moduleInfos as a copy: {plugin slug: {module slug: ModuleInfo}}. Assign a whole mapping, or change one "
		"with get_module_info.");
	cls.def_property("module_whitelist", [](const Settings&) {
		requireMain("reading Rack's settings");
		return st::moduleWhitelist;
	}, [](const Settings&, std::map<std::string, st::PluginWhitelist> whitelist) {
		requireMain("changing Rack's settings");
		st::moduleWhitelist = std::move(whitelist);
	}, "Rack's moduleWhitelist as a copy: {plugin slug: PluginWhitelist}.");
	m.attr("settings") = py::cast(Settings{});

	m.def("get_module_info", [](const std::string& plugin, const std::string& module) -> py::object {
		requireMain("using Rack's settings");
		if (!st::getModuleInfo(plugin, module))
			return py::none();
		return py::cast(ModuleInfoView{plugin, module});
	}, py::arg("plugin_slug"), py::arg("module_slug"), "Rack's: the module's info in its settings, or None.");
	m.def("is_module_whitelisted", [](const std::string& plugin, const std::string& module) {
		requireMain("using Rack's settings");
		return st::isModuleWhitelisted(plugin, module);
	}, py::arg("plugin_slug"), py::arg("module_slug"));
	m.def("reset_cables", [] {
		requireMain("changing Rack's settings");
		st::resetCables();
	}, "Rack's: the default cable colours and labels.");
}

}  // namespace services
}  // namespace bindings
}  // namespace rackpy
