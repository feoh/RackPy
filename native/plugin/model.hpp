// SPDX-License-Identifier: GPL-3.0-or-later
// Python models (RP-16A): Rack's createModel<TModule, TModuleWidget> for a
// module factory written in Python. This part is Python-free: the factory is
// an interface the bindings implement, so Rack's model and placeholder code
// never touches the interpreter.
#pragma once
#include <rack.hpp>

#include <memory>
#include <string>
#include <vector>

namespace rackpy {
namespace plugin {

/** Builds one module for Rack. The bindings implement it with a Python callable. */
struct ModuleFactory {
	virtual ~ModuleFactory() = default;
	/** A new module whose model is `model`, or null if the factory failed (already diagnosed). Never throws. */
	virtual rack::engine::Module* create(rack::plugin::Model& model) noexcept = 0;
	/** Drop every Python reference. Called with the GIL before the interpreter finalizes. */
	virtual void release() noexcept = 0;
};

/** Rack gets this when a Python factory fails: Rack asserts that createModule
returns a module and never catches. It has no components and keeps the
patch's JSON for the module, writing it back unchanged, so a broken script
never destroys a patch. */
struct PlaceholderModule : rack::engine::Module {
	json_t* saved = nullptr;
	~PlaceholderModule() override;
	json_t* toJson() override;
	void fromJson(json_t* rootJ) override;
};

/** A model whose modules come from a factory. Rack deletes it with its plugin,
after destroy(), so its destructor must not touch Python: the factory has been
released by then. */
struct Model : rack::plugin::Model {
	std::unique_ptr<ModuleFactory> factory;
	int hp = 4;
	Model();
	~Model() override;
	rack::engine::Module* createModule() override;
	rack::app::ModuleWidget* createModuleWidget(rack::engine::Module* m) override;
};

/** The RackPy plugin, as Rack passed it to init(); null before then. */
void setInstance(rack::plugin::Plugin* plugin);
rack::plugin::Plugin* instance();

/** Registration is open only while the host initializes the plugin. */
void openRegistration();
bool registrationOpen();
/** Stage a model for commit(), as Plugin::addModel: sets its plugin. Refuses
a duplicate, empty or invalid slug (std::invalid_argument) and a closed
registration (std::logic_error). Takes `model` only on success. */
void stage(std::unique_ptr<Model>& model);
/** The staged models, in registration order. */
const std::vector<std::unique_ptr<Model>>& staged();
/** Check `plugin`'s natively added models plus the staged ones against the
plugin.json at `manifestPath`, then add the staged ones. Closes registration.
Returns an empty string on success, or why nothing was added. */
std::string commit(rack::plugin::Plugin* plugin, const std::string& manifestPath);
/** Drop the staged models (the entry module failed): releases their factories. Closes registration. */
void discard() noexcept;
/** Release every live model's factory (before the interpreter finalizes). */
void releaseFactories() noexcept;

}  // namespace plugin
}  // namespace rackpy
