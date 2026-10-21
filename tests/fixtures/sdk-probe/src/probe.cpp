// SPDX-License-Identifier: GPL-3.0-or-later
// RP-02 baseline probe: a plugin translation unit that includes only rack.hpp.
#include <rack.hpp>

using namespace rack;

Plugin* pluginInstance;

struct ProbeModule : Module {
	ProbeModule() {
		config(0, 0, 0, 0);
	}
	void process(const ProcessArgs& args) override {}
};

struct ProbeModuleWidget : ModuleWidget {
	ProbeModuleWidget(ProbeModule* module) {
		setModule(module);
	}
};

extern "C" void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(createModel<ProbeModule, ProbeModuleWidget>("Probe"));
}
