// SPDX-License-Identifier: GPL-3.0-or-later
// C++ reference for examples/configuration (RP-34): the Shaper and the
// Splitter as a C++ plugin author writes them, on Rack's own Module,
// configParam, configSwitch, configButton, configInput/Output/Light,
// configBypass and processBypass. It returns, as JSON, what Rack shows for
// each control (its tooltip strings at several values), the ports' and
// light's names and descriptions, the bypass routes, and the outputs over
// fixed scenarios, processed and bypassed. Rack's ParamQuantity reaches the
// parameter through APP->engine, so this runs only inside live Rack, as the
// test-hooks build's _testing.controls_reference(); tests/examples replays
// the scenarios on the example's modules there and compares.
#pragma once
#include <rack.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace rackpy_controls_reference {

using namespace rack;

struct Shaper : engine::Module {
	enum ParamId { LEVEL, SCALE, OFFSET, DEPTH, RATE, MODE, MUTE, PARAMS_LEN };
	enum InputId { SIGNAL, INPUTS_LEN };
	enum OutputId { SHAPED, OUTPUTS_LEN };
	enum LightId { LFO, LIGHTS_LEN };
	float phase = 0.f;

	Shaper() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(LEVEL, 0.f, 2.f, 1.f, "Level", " dB", -10.f, 20.f);
		configParam(SCALE, -1.f, 1.f, 1.f, "Scale");
		configParam(OFFSET, -5.f, 5.f, 0.f, "Offset", " V");
		configParam(DEPTH, 0.f, 1.f, 0.f, "Tremolo depth", "%", 0.f, 100.f);
		configParam(RATE, -3.f, 3.f, 0.f, "Tremolo rate", " Hz", 2.f, 1.f);
		getParamQuantity(RATE)->description = "Doubles with each sixth of a turn; the light follows the tremolo.";
		configSwitch(MODE, 0.f, 2.f, 0.f, "Shape", {"Bipolar", "Rectified", "Positive half"});
		configButton(MUTE, "Mute while held");
		configInput(SIGNAL, "Signal");
		configOutput(SHAPED, "Shaped");
		getOutputInfo(SHAPED)->description =
			"Shape, then level, scale and tremolo, plus offset; polyphonic like the input.";
		configLight(LFO, "Tremolo");
		configBypass(SIGNAL, SHAPED);
	}

	void process(const ProcessArgs& args) override {
		phase += std::pow(2.f, params[RATE].getValue()) * args.sampleTime;
		if (phase >= 1.f)
			phase -= 1.f;
		float lfo = 0.5f + 0.5f * std::sin(2.f * float(M_PI) * phase);
		float gain = params[LEVEL].getValue() * params[SCALE].getValue() * (1.f - params[DEPTH].getValue() * lfo);
		int mode = (int) std::round(params[MODE].getValue());
		bool mute = params[MUTE].getValue() > 0.5f;
		int channels = std::max(1, inputs[SIGNAL].getChannels());
		for (int c = 0; c < channels; c++) {
			float x = inputs[SIGNAL].getVoltage(c);
			if (mode == 1)
				x = std::fabs(x);
			else if (mode == 2)
				x = std::fmax(x, 0.f);
			outputs[SHAPED].setVoltage(mute ? 0.f : x * gain + params[OFFSET].getValue(), c);
		}
		outputs[SHAPED].setChannels(channels);
		lights[LFO].setBrightness(lfo);
	}
};

struct Splitter : engine::Module {
	enum ParamId { LEVEL, PARAMS_LEN };
	enum InputId { SIGNAL, INPUTS_LEN };
	enum OutputId { THROUGH, INVERTED, OUTPUTS_LEN };

	Splitter() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN);
		configParam(LEVEL, 0.f, 1.f, 1.f, "Level", "%", 0.f, 100.f);
		configInput(SIGNAL, "Signal");
		configOutput(THROUGH, "Through");
		configOutput(INVERTED, "Inverted");
		getOutputInfo(INVERTED)->description = "Bypassed, still inverted, at unity gain.";
	}

	void split(float gain) {
		int channels = std::max(1, inputs[SIGNAL].getChannels());
		for (int c = 0; c < channels; c++) {
			float x = inputs[SIGNAL].getVoltage(c) * gain;
			outputs[THROUGH].setVoltage(x, c);
			outputs[INVERTED].setVoltage(-x, c);
		}
		outputs[THROUGH].setChannels(channels);
		outputs[INVERTED].setChannels(channels);
	}

	void process(const ProcessArgs&) override { split(params[LEVEL].getValue()); }
	// A custom bypass: unity gain, still inverted. Rack's routes could only copy.
	void processBypass(const ProcessArgs&) override { split(1.f); }
};

inline std::string str(const std::string& x) {
	std::string out = "\"";
	for (char c : x) {
		if (c == '"' || c == '\\')
			out += '\\';
		out += c;
	}
	return out + "\"";
}

inline std::string num(float x) {
	char buf[32];
	std::snprintf(buf, sizeof buf, "%.9g", x);
	return buf;
}

template <typename T>
inline std::string list(const std::vector<T>& items, std::string (*one)(const T&)) {
	std::string out = "[";
	for (size_t i = 0; i < items.size(); i++)
		out += (i ? ", " : "") + one(items[i]);
	return out + "]";
}

inline std::string strOf(const std::string& s) {
	return str(s);
}

inline std::string numOf(const float& x) {
	return num(x);
}

/** Each control as Rack shows it: its fields, and its tooltip strings at each value. */
inline std::string controls(engine::Module& m) {
	std::string out = "[";
	for (int id = 0; id < (int) m.params.size(); id++) {
		engine::ParamQuantity* q = m.getParamQuantity(id);
		float lo = q->getMinValue(), hi = q->getMaxValue();
		std::vector<float> values = {lo, hi, q->getDefaultValue(), lo + (hi - lo) * 0.25f, lo + (hi - lo) * 0.5f};
		if (lo < 0.f && hi > 0.f)
			values.push_back(0.f);
		std::string cases = "[";
		for (size_t i = 0; i < values.size(); i++) {
			q->setValue(values[i]);
			cases += std::string(i ? ", " : "") + "[" + num(values[i]) + ", " + num(q->getValue()) + ", " +
				str(q->getDisplayValueString()) + ", " + str(q->getString()) + "]";
		}
		q->reset();
		cases += "]";
		auto* sw = dynamic_cast<engine::SwitchQuantity*>(q);
		out += std::string(id ? ", " : "") + "{\"name\": " + str(q->name) + ", \"label\": " + str(q->getLabel()) +
			", \"unit\": " + str(q->getUnit()) + ", \"description\": " + str(q->getDescription()) +
			", \"min\": " + num(lo) + ", \"max\": " + num(hi) + ", \"default\": " + num(q->getDefaultValue()) +
			", \"snap\": " + (q->snapEnabled ? "true" : "false") + ", \"smooth\": " + (q->smoothEnabled ? "true" : "false") +
			", \"switch\": " + (sw ? "true" : "false") + ", \"labels\": " + (sw ? list(sw->labels, strOf) : "[]") +
			", \"cases\": " + cases + "}";
	}
	return out + "]";
}

inline std::string ports(std::vector<engine::PortInfo*>& infos) {
	std::string out = "[";
	for (size_t i = 0; i < infos.size(); i++)
		out += std::string(i ? ", " : "") + "[" + str(infos[i]->getName()) + ", " + str(infos[i]->getFullName()) + ", " +
			str(infos[i]->getDescription()) + "]";
	return out + "]";
}

inline std::string lights(engine::Module& m) {
	std::string out = "[";
	for (size_t i = 0; i < m.lightInfos.size(); i++)
		out += std::string(i ? ", " : "") + "[" + str(m.lightInfos[i]->getName()) + ", " +
			str(m.lightInfos[i]->getDescription()) + "]";
	return out + "]";
}

inline std::string routes(engine::Module& m) {
	std::string out = "[";
	for (size_t i = 0; i < m.bypassRoutes.size(); i++)
		out += std::string(i ? ", " : "") + "[" + std::to_string(m.bypassRoutes[i].inputId) + ", " +
			std::to_string(m.bypassRoutes[i].outputId) + "]";
	return out + "]";
}

/** A scenario: parameter values (an optional button press over [pressFrom, pressTo)), input channels. */
struct Scenario {
	const char* name;
	std::vector<float> params;
	int channels;
	bool bypassed;
	int press = -1;  // a parameter held at 1 between pressFrom and pressTo
	int pressFrom = 0, pressTo = 0;
};

inline float signal(int frame, int channel) {
	return 5.f * std::sin(2.f * float(M_PI) * (frame / 97.f + channel * 0.3f)) + (channel - 1) * 0.5f;
}

/** Steps a module through a scenario; returns the scenario and each frame's outputs (and lights) as JSON. */
template <typename M>
inline std::string run(const Scenario& s, int frames, float rate) {
	M m;
	for (size_t i = 0; i < s.params.size(); i++)
		m.params[i].setValue(s.params[i]);
	m.inputs[0].channels = (uint8_t) s.channels;  // as Rack marks a patched input
	for (auto& o : m.outputs)
		o.channels = 1;  // patched outputs
	engine::Module::ProcessArgs args;
	args.sampleRate = rate;
	args.sampleTime = 1.f / rate;
	std::string out = "[";
	for (int k = 0; k < frames; k++) {
		args.frame = k;
		for (int c = 0; c < s.channels; c++)
			m.inputs[0].setVoltage(signal(k, c), c);
		if (s.press >= 0)
			m.params[s.press].setValue(k >= s.pressFrom && k < s.pressTo ? 1.f : 0.f);
		if (s.bypassed)
			m.processBypass(args);
		else
			m.process(args);
		// Each frame: the input's voltages, then each output's, then each light's brightness.
		std::vector<float> in(m.inputs[0].getVoltages(), m.inputs[0].getVoltages() + s.channels);
		std::string frame = "[" + list(in, numOf);
		for (size_t o = 0; o < m.outputs.size(); o++) {
			std::vector<float> volts(m.outputs[o].getVoltages(), m.outputs[o].getVoltages() + m.outputs[o].getChannels());
			frame += ", " + list(volts, numOf);
		}
		for (auto& light : m.lights)
			frame += ", " + num(light.getBrightness());
		out += std::string(k ? ", " : "") + frame + "]";
	}
	std::string params = "[";
	for (size_t i = 0; i < s.params.size(); i++)
		params += std::string(i ? ", " : "") + num(s.params[i]);
	params += "]";
	return std::string("{\"name\": ") + str(s.name) + ", \"params\": " + params + ", \"channels\": " +
		std::to_string(s.channels) + ", \"bypassed\": " + (s.bypassed ? "true" : "false") + ", \"press\": [" +
		std::to_string(s.press) + ", " + std::to_string(s.pressFrom) + ", " + std::to_string(s.pressTo) +
		"], \"frames\": " + out + "]}";
}

template <typename M>
inline std::string describe(const std::vector<Scenario>& scenarios, int frames, float rate) {
	M m;
	std::string runs = "[";
	for (size_t i = 0; i < scenarios.size(); i++)
		runs += std::string(i ? ", " : "") + run<M>(scenarios[i], frames, rate);
	runs += "]";
	return "{\"controls\": " + controls(m) + ", \"inputs\": " + ports(m.inputInfos) + ", \"outputs\": " +
		ports(m.outputInfos) + ", \"lights\": " + lights(m) + ", \"routes\": " + routes(m) + ", \"scenarios\": " +
		runs + "}";
}

/** The reference, as JSON text. Main thread, in live Rack, which is already in its floating-point mode. */
inline std::string controlsReference() {
	const int frames = 2400;
	const float rate = 48000.f;
	// level, scale, offset, depth, rate, mode, mute
	std::vector<Scenario> shaper = {
		{"defaults", {1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f}, 1, false},
		{"rectified tremolo", {0.5f, -1.f, 1.5f, 0.5f, 1.f, 1.f, 0.f}, 1, false},
		{"positive half, muted a while", {2.f, 0.25f, 0.f, 1.f, 3.f, 2.f, 0.f}, 1, false, Shaper::MUTE, 600, 1500},
		{"polyphonic", {1.5f, 0.75f, -1.f, 0.3f, -2.f, 0.f, 0.f}, 3, false},
		{"unpatched", {1.f, 1.f, 2.5f, 0.f, 0.f, 0.f, 0.f}, 0, false},
		{"bypassed routes", {0.5f, -1.f, 1.5f, 0.5f, 1.f, 1.f, 0.f}, 3, true},
	};
	std::vector<Scenario> splitter = {
		{"level", {0.4f}, 1, false},
		{"polyphonic", {0.8f}, 4, false},
		{"custom bypass", {0.4f}, 4, true},
	};
	std::string shaped = describe<Shaper>(shaper, frames, rate);
	std::string split = describe<Splitter>(splitter, frames, rate);
	return "{\"rate\": " + num(rate) + ", \"shaper\": " + shaped + ", \"splitter\": " + split + "}";
}

}  // namespace rackpy_controls_reference
