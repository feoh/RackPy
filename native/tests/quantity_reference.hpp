// SPDX-License-Identifier: GPL-3.0-or-later
// Native SDK differential fixture, called only in a live Rack test context.
#pragma once
#include <rack.hpp>

inline json_t* quantityReference() {
	json_t* quantities = json_array();
	for (float base : {0.f, 2.f, -2.f}) {
		rack::engine::Module m;
		m.config(3, 0, 0);
		auto q = m.configParam(0, 1.f, 8.f, 2.f, "Pitch", " Hz", base, 3.f, 4.f);
		auto s = m.configSwitch(1, -1.f, 1.f, 0.f, "Mode", {"Low", "Middle", "High"});
		auto b = m.configButton(2, "Trigger");
		json_t* c = json_pack("{s:f,s:[fff],s:[bbb],s:[bbb]}", "base", base,
			"defaults", m.params[0].value, m.params[1].value, m.params[2].value,
			"switch_flags", s->snapEnabled, s->smoothEnabled, s->randomizeEnabled,
			"button_flags", b->snapEnabled, b->smoothEnabled, b->randomizeEnabled);
		json_array_append_new(quantities, c);
		json_t* values = json_array();
		json_object_set_new(c, "values", values);
		for (float x : {-10.f, 1.f, 2.5f, 8.f, 20.f}) {
			q->setValue(x);
			json_array_append_new(values, json_pack("[fffs]", x, q->getValue(), q->getDisplayValue(), q->getDisplayValueString().c_str()));
		}
		json_t* displays = json_array();
		json_object_set_new(c, "displays", displays);
		for (float x : {4.f, 10.f, 28.f}) {
			q->setDisplayValue(x);
			json_array_append_new(displays, json_pack("[ff]", x, q->getImmediateValue()));
		}
		q->setDisplayValueString("2 + 8");
		json_object_set_new(c, "parsed", json_real(q->getImmediateValue()));
		q->reset();
		json_object_set_new(c, "reset", json_real(q->getValue()));
		json_t* switches = json_array();
		json_object_set_new(c, "switches", switches);
		for (float x : {-10.f, -0.5f, 0.49f, 0.5f, 10.f}) {
			s->setImmediateValue(x);
			json_array_append_new(switches, json_pack("[ffs]", x, s->getValue(), s->getDisplayValueString().c_str()));
		}
		s->setDisplayValueString("mIDdle");
		json_object_set_new(c, "switch_parsed", json_real(s->getValue()));
	}
	return quantities;
}
