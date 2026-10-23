// SPDX-License-Identifier: GPL-3.0-or-later
// Rack's own construction and bypass results; replayed inside Rack in Python.
#include <rack.hpp>

#include <cstdio>
#include <type_traits>

// RP-14B2b5: class factories construct the subclass, then set Rack's fields.
static int deletedInfos = 0;
template <typename Base>
struct CustomInfo : Base {
	CustomInfo() { this->description = "From constructor"; }
	~CustomInfo() override { deletedInfos++; }
	std::string getName() override { return "Custom " + this->name; }
};

// RP-14B2b6: the quantity templates keep everything else the constructor set.
template <typename Base>
struct CustomQuantity : Base {
	CustomQuantity() {
		this->description = "From constructor";
		this->randomizeEnabled = false;
		this->displayPrecision = 3;
		if constexpr (std::is_base_of<rack::engine::SwitchQuantity, Base>::value)
			this->labels = {"Off", "On"};
	}
	~CustomQuantity() override { deletedInfos++; }
	std::string getLabel() override { return "Custom " + this->name; }
};

json_t* quantityRecord(rack::engine::Module& m, int id) {
	rack::engine::ParamQuantity* q = m.paramQuantities[id];
	json_t* labels = json_array();
	if (auto* s = dynamic_cast<rack::engine::SwitchQuantity*>(q))
		for (const auto& label : s->labels)
			json_array_append_new(labels, json_string(label.c_str()));
	return json_pack("[ssssiffffffibbbbfo]", q->getLabel().c_str(), q->description.c_str(), q->name.c_str(),
		q->unit.c_str(), q->paramId, q->minValue, q->maxValue, q->defaultValue, q->displayBase,
		q->displayMultiplier, q->displayOffset, q->displayPrecision, q->snapEnabled, q->smoothEnabled,
		q->randomizeEnabled, q->module == &m, m.params[id].value, labels);
}

int main() {
	json_t* root = json_object();
	json_t* cases = json_array();
	json_object_set_new(root, "cases", cases);
	for (int n : {0, 1, rack::engine::PORT_MAX_CHANNELS}) {
		rack::engine::Module m;
		m.config(n, n, n, n);
		json_t* c = json_object();
		json_object_set_new(c, "n", json_integer(n));
		json_object_set_new(c, "counts", json_pack("[iiii]", m.getNumParams(), m.getNumInputs(), m.getNumOutputs(), m.getNumLights()));
		json_object_set_new(c, "params", json_array());
		json_object_set_new(c, "inputs", json_array());
		json_object_set_new(c, "outputs", json_array());
		json_object_set_new(c, "lights", json_array());
		for (int i = 0; i < n; i++) {
			json_array_append_new(json_object_get(c, "params"), json_real(m.getParam(i).getValue()));
			json_array_append_new(json_object_get(c, "inputs"), json_integer(m.getInput(i).getChannels()));
			json_array_append_new(json_object_get(c, "outputs"), json_integer(m.getOutput(i).getChannels()));
			json_array_append_new(json_object_get(c, "lights"), json_real(m.getLight(i).getBrightness()));
		}
		json_array_append_new(cases, c);
	}
	// The public standalone pointer fields are nullable and reassignable;
	// RackPy additionally clears them when the borrowed owner is destroyed.
	{
		rack::engine::Module first, second;
		rack::engine::PortInfo port;
		rack::engine::LightInfo light;
		json_t* links = json_array();
		json_array_append_new(links, json_boolean(port.module == nullptr && light.module == nullptr));
		port.module = light.module = &first;
		json_array_append_new(links, json_boolean(port.module == &first && light.module == &first));
		port.module = light.module = &second;
		json_array_append_new(links, json_boolean(port.module == &second && light.module == &second));
		port.module = light.module = nullptr;
		json_array_append_new(links, json_boolean(port.module == nullptr && light.module == nullptr));
		json_object_set_new(root, "standalone_backreferences", links);
	}
	// Standalone quantities need no Engine while their module pointer is null.
	json_t* standalone = json_array();
	json_object_set_new(root, "standalone_quantities", standalone);
	for (float base : {0.f, 2.f}) {
		rack::engine::ParamQuantity q;
		q.minValue = -2.f; q.maxValue = 3.f; q.defaultValue = 1.f;
		q.displayBase = base; q.displayMultiplier = 3.f; q.displayOffset = 4.f;
		q.paramId = 2; q.unit = " V"; q.description = "Description";
		q.setValue(2.f); q.setImmediateValue(1.f); q.setDisplayValue(7.f);
		q.setDisplayValueString("2 + 8"); q.reset(); q.randomize();
		json_array_append_new(standalone, json_pack("{s:f,s:[fff],s:[sss],s:b,s:b}",
			"base", base, "values", q.getValue(), q.getImmediateValue(), q.getDisplayValue(),
			"text", q.getLabel().c_str(), q.getUnit().c_str(), q.getDescription().c_str(),
			"null", q.module == nullptr && q.getParam() == nullptr, "bounded", q.isBounded()));
	}
	json_t* metadata = json_array();
	json_object_set_new(root, "metadata", metadata);
	for (std::string name : {"", "Pitch", "λ / 𝄞"}) {
		rack::engine::Module m;
		m.config(0, 2, 2, 2);
		json_t* c = json_object();
		json_array_append_new(metadata, c);
		json_object_set_new(c, "name", json_string(name.c_str()));
		json_t* defaults = json_array();
		json_object_set_new(c, "defaults", defaults);
		for (int i = 0; i < 2; ++i) {
			json_array_append_new(defaults, json_pack("[ssb]", m.getInputInfo(i)->getName().c_str(),
				m.getOutputInfo(i)->getName().c_str(), m.getLightInfo(i) == nullptr));
		}
		json_t* configured = json_array();
		json_object_set_new(c, "configured", configured);
		for (int i = 0; i < 2; ++i) {
			auto input = m.configInput(i, name);
			auto output = m.configOutput(i, name);
			auto light = m.configLight(i, name);
			input->description = output->description = light->description = "A description";
			// getFullName() uses Rack's loaded translation catalogue, absent
			// from this standalone fixture. Test that separately in actual Rack.
			json_array_append_new(configured, json_pack("[issssssbbbbb]", i,
				input->getName().c_str(), input->getDescription().c_str(),
				output->getName().c_str(), output->getDescription().c_str(),
				light->getName().c_str(), light->getDescription().c_str(),
				input->module == &m, output->module == &m, light->module == &m,
				input->type == rack::engine::Port::INPUT, output->type == rack::engine::Port::OUTPUT));
		}
	}
	{
		using rack::engine::LightInfo;
		using rack::engine::PortInfo;
		json_t* factories = json_array();
		json_object_set_new(root, "factories", factories);
		rack::engine::Module m;
		m.config(0, 1, 1, 1);
		auto* input = m.configInput<CustomInfo<PortInfo>>(0, "Pitch");
		auto* output = m.configOutput<CustomInfo<PortInfo>>(0, "Out");
		auto* light = m.configLight<CustomInfo<LightInfo>>(0, "Gate");
		for (PortInfo* port : {static_cast<PortInfo*>(input), static_cast<PortInfo*>(output)}) {
			json_array_append_new(factories, json_pack("[sssibbb]", port->getName().c_str(),
				port->getDescription().c_str(), port->name.c_str(), port->portId,
				port->type == rack::engine::Port::INPUT, port->module == &m,
				port == (port == input ? m.inputInfos[0] : m.outputInfos[0])));
		}
		json_array_append_new(factories, json_pack("[sssibbb]", light->getName().c_str(),
			light->getDescription().c_str(), light->name.c_str(), light->lightId, false,
			light->module == &m, light == m.lightInfos[0]));
		// Replacing a slot deletes the previous object; a built-in replaces it too.
		auto* again = m.configInput<CustomInfo<PortInfo>>(0, "Again");
		json_t* replaced = json_pack("[ibs]", deletedInfos, m.inputInfos[0] == again, again->getName().c_str());
		m.configInput(0, "Plain");
		json_array_append_new(replaced, json_integer(deletedInfos));
		json_array_append_new(replaced, json_string(m.inputInfos[0]->getName().c_str()));
		json_object_set_new(root, "factory_replacement", replaced);
	}
	{
		using rack::engine::ParamQuantity;
		using rack::engine::SwitchQuantity;
		rack::engine::Module m;
		m.config(4, 0, 0, 0);
		m.configParam<CustomQuantity<ParamQuantity>>(0, -1.f, 1.f, 0.5f, "Gain", " dB", 0.f, 2.f, 3.f);
		m.configSwitch<CustomQuantity<SwitchQuantity>>(1, 0.f, 2.f, 1.f, "Mode", {"X", "Y", "Z"});
		m.configButton<CustomQuantity<SwitchQuantity>>(2, "Push");
		// configParam with a switch class: no switch fields are touched.
		m.configParam<CustomQuantity<SwitchQuantity>>(3, 0.f, 4.f, 2.f, "Steps");
		json_t* quantities = json_array();
		for (int i = 0; i < 4; i++)
			json_array_append_new(quantities, quantityRecord(m, i));
		json_object_set_new(root, "quantity_factories", quantities);
		int before = deletedInfos;
		m.configParam<CustomQuantity<ParamQuantity>>(0, 0.f, 1.f, 0.25f, "Again");
		json_t* replaced = json_pack("[if]", deletedInfos - before, m.params[0].value);
		m.configParam(0, 0.f, 1.f, 0.75f, "Plain");
		json_array_append_new(replaced, json_integer(deletedInfos - before));
		json_array_append_new(replaced, json_string(m.paramQuantities[0]->getLabel().c_str()));
		json_array_append_new(replaced, json_real(m.params[0].value));
		json_object_set_new(root, "quantity_replacement", replaced);
	}
	json_t* bypass = json_array();
	json_object_set_new(root, "bypass", bypass);
	for (int channels : {0, 1, rack::engine::PORT_MAX_CHANNELS}) {
		rack::engine::Module m;
		m.config(0, 1, 1);
		m.configBypass(0, 0);
		m.inputs[0].channels = channels;  // native fixture: a connected input
		m.outputs[0].channels = 1;  // connected output
		for (int i = 0; i < channels; i++)
			m.inputs[0].setVoltage(i * 0.25f - 2.f, i);
		m.processBypass({48000.f, 1.f / 48000.f, 0});
		json_t* c = json_pack("{s:i,s:i}", "input_channels", channels, "output_channels", m.outputs[0].getChannels());
		json_t* voltages = json_array();
		json_object_set_new(c, "voltages", voltages);
		for (int i = 0; i < rack::engine::PORT_MAX_CHANNELS; i++)
			json_array_append_new(voltages, json_real(m.outputs[0].getVoltage(i)));
		json_array_append_new(bypass, c);
	}
	json_dumpf(root, stdout, JSON_INDENT(2));
	json_decref(root);
	return 0;
}
