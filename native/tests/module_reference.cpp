// SPDX-License-Identifier: GPL-3.0-or-later
// Rack's own construction and bypass results; replayed inside Rack in Python.
#include <rack.hpp>

#include <cstdio>

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
