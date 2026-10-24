// SPDX-License-Identifier: GPL-3.0-or-later
#include "model.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <set>
#include <stdexcept>

namespace rackpy {
namespace plugin {

namespace {

bool open = false;
rack::plugin::Plugin* pluginInstance = nullptr;
std::vector<std::unique_ptr<Model>> pending;
// Committed models, which Rack owns, until their destructors run.
std::vector<Model*> live;

/** An original, plain panel until Python module widgets exist (RP-28). */
struct BlankPanel : rack::widget::Widget {
	bool failed = false;
	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, failed ? nvgRGB(0x5c, 0x30, 0x30) : nvgRGB(0x3b, 0x3f, 0x47));
		nvgFill(args.vg);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStrokeColor(args.vg, nvgRGB(0x1f, 0x21, 0x26));
		nvgStroke(args.vg);
		Widget::draw(args);
	}
};

// Fields the placeholder's own state decides; everything else comes from the saved JSON.
bool ownField(const char* key) {
	for (const char* own : {"id", "plugin", "model", "version", "bypass", "leftModuleId", "rightModuleId"})
		if (!std::strcmp(key, own))
			return true;
	return false;
}

}  // namespace

PlaceholderModule::~PlaceholderModule() {
	if (saved)
		json_decref(saved);
}

void PlaceholderModule::fromJson(json_t* rootJ) {
	rack::engine::Module::fromJson(rootJ);  // ID, bypass and expanders; it has no parameters
	json_t* copy = json_deep_copy(rootJ);
	if (saved)
		json_decref(saved);
	saved = copy;
}

json_t* PlaceholderModule::toJson() {
	json_t* rootJ = rack::engine::Module::toJson();
	if (!saved)
		return rootJ;
	const char* key;
	json_t* value;
	json_object_foreach(saved, key, value) {
		if (!ownField(key))
			json_object_set_new(rootJ, key, json_deep_copy(value));  // replaces the placeholder's empty "params"
	}
	return rootJ;
}

Model::Model() = default;

Model::~Model() {
	live.erase(std::remove(live.begin(), live.end(), this), live.end());
}

rack::engine::Module* Model::createModule() {
	rack::engine::Module* m = factory ? factory->create(*this) : nullptr;
	if (m)
		return m;
	WARN("RackPy: model %s could not create its module; using a placeholder that keeps the module's saved state",
		slug.c_str());
	auto* placeholder = new PlaceholderModule;
	placeholder->model = this;
	return placeholder;
}

rack::app::ModuleWidget* Model::createModuleWidget(rack::engine::Module* m) {
	// As Rack's createModel: the widget gets the module (or null, for a browser
	// preview), then the model.
	auto* mw = new rack::app::ModuleWidget;
	mw->setModule(m);
	mw->setModel(this);
	mw->box.size = rack::math::Vec(rack::RACK_GRID_WIDTH * hp, rack::RACK_GRID_HEIGHT);
	auto* panel = new BlankPanel;
	panel->box.size = mw->box.size;
	panel->failed = dynamic_cast<PlaceholderModule*>(m) != nullptr;
	mw->addChild(panel);
	auto* label = new rack::ui::Label;
	label->box.pos = rack::math::Vec(0.f, 12.f);
	label->box.size = rack::math::Vec(mw->box.size.x, 20.f);
	label->alignment = rack::ui::Label::CENTER_ALIGNMENT;
	label->color = nvgRGB(0xe6, 0xe8, 0xec);
	label->text = panel->failed ? name + " (failed)" : name;
	mw->addChild(label);
	return mw;
}

void setInstance(rack::plugin::Plugin* plugin) {
	pluginInstance = plugin;
}

rack::plugin::Plugin* instance() {
	return pluginInstance;
}

void openRegistration() {
	open = true;
}

bool registrationOpen() {
	return open;
}

void stage(std::unique_ptr<Model>& model) {
	if (!open)
		throw std::logic_error("models can be added only while the plugin initializes, from its entry module");
	if (model->slug.empty())
		throw std::invalid_argument("model slug cannot be empty");
	if (!rack::plugin::isSlugValid(model->slug))
		throw std::invalid_argument("model slug \"" + model->slug + "\" is invalid: use letters, digits, - and _");
	for (const auto& other : pending)
		if (other->slug == model->slug)
			throw std::invalid_argument("a model with slug \"" + model->slug + "\" is already added");
	if (model->plugin)
		throw std::logic_error("this model has already been added to a plugin");
	model->plugin = pluginInstance;
	pending.push_back(std::move(model));
}

const std::vector<std::unique_ptr<Model>>& staged() {
	return pending;
}

namespace {

std::string manifestSlugs(const std::string& path, std::vector<std::string>& slugs) {
	json_error_t error;
	json_t* rootJ = json_load_file(path.c_str(), 0, &error);
	if (!rootJ)
		return "cannot read " + path + ": " + error.text;
	std::string problem;
	json_t* modulesJ = json_object_get(rootJ, "modules");
	if (modulesJ && !json_is_array(modulesJ))
		problem = path + ": \"modules\" is not a list";
	size_t i;
	json_t* moduleJ;
	json_array_foreach(modulesJ, i, moduleJ) {
		const char* slug = json_string_value(json_object_get(moduleJ, "slug"));
		if (!slug) {
			problem = path + ": module entry #" + std::to_string(i) + " has no slug";
			break;
		}
		slugs.push_back(slug);
	}
	json_decref(rootJ);
	return problem;
}

std::string joined(const std::set<std::string>& names) {
	std::string out;
	for (const auto& name : names)
		out += (out.empty() ? "" : ", ") + name;
	return out;
}

}  // namespace

std::string commit(rack::plugin::Plugin* plugin, const std::string& manifestPath) {
	open = false;
	std::vector<std::string> listed;
	std::string problem = manifestSlugs(manifestPath, listed);
	std::set<std::string> manifest(listed.begin(), listed.end());
	std::set<std::string> registered;
	for (auto* model : plugin->models)
		registered.insert(model->slug);
	for (const auto& model : pending) {
		if (!registered.insert(model->slug).second && problem.empty())
			problem = "model slug \"" + model->slug + "\" is registered twice";
	}
	if (problem.empty() && manifest.size() != listed.size())
		problem = manifestPath + " lists a module slug more than once";
	if (problem.empty() && manifest != registered) {
		std::set<std::string> missing, extra;
		std::set_difference(manifest.begin(), manifest.end(), registered.begin(), registered.end(),
			std::inserter(missing, missing.end()));
		std::set_difference(registered.begin(), registered.end(), manifest.begin(), manifest.end(),
			std::inserter(extra, extra.end()));
		problem = "the entry module's models do not match plugin.json's \"modules\"";
		if (!missing.empty())
			problem += "; listed but not added: " + joined(missing);
		if (!extra.empty())
			problem += "; added but not listed: " + joined(extra);
		problem += ". Rack rejects a plugin unless they match exactly.";
	}
	if (!problem.empty()) {
		for (auto& model : pending)
			if (model->factory)
				model->factory->release();
		pending.clear();
		return problem;
	}
	for (auto& model : pending) {
		live.push_back(model.get());
		model->plugin = nullptr;  // Plugin::addModel asserts that, then sets it again
		plugin->addModel(model.release());
	}
	pending.clear();
	return "";
}

void discard() noexcept {
	open = false;
	for (auto& model : pending)
		if (model->factory)
			model->factory->release();
	pending.clear();
}

void releaseFactories() noexcept {
	for (auto& model : pending)
		if (model->factory)
			model->factory->release();
	for (auto* model : live)
		if (model->factory)
			model->factory->release();
}

}  // namespace plugin
}  // namespace rackpy
