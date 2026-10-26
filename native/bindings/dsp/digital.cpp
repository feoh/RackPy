// SPDX-License-Identifier: GPL-3.0-or-later
// dsp/digital.hpp -> rackpy.raw.dsp (RP-18B): triggers, the pulse generator,
// timers and the clock divider, as stateful native Python objects.
//
// Each object holds Rack's C++ struct, and every method runs Rack's inline
// code. They are values Python owns: use one from one thread at a time (each
// call holds the GIL). Rack's templates TSchmittTrigger<T> and TTimer<T> are
// subscriptable: TSchmittTrigger[float] is Rack's SchmittTrigger (its explicit
// specialization) and TSchmittTrigger[float_4] the generic template for
// vectors, also named TSchmittTrigger_float_4. These are comparisons, single
// additions and bit operations, so they need no out-of-line wrappers
// (calls.hpp).
#include "dsp.hpp"

#include <pybind11/native_enum.h>

#include <string>

#include "../simd/simd.hpp"
#include "../support/overrides.hpp"
#include "../values/values.hpp"

namespace rackpy {
namespace dsp_bindings {

namespace d = rack::dsp;
using rack::simd::float_4;

namespace {

uint32_t u32(const py::int_& v, const char* what) {
	return values::toInt<uint32_t>(v, what, "uint32_t");
}

std::string stateName(int s) {
	return s == 0 ? "LOW" : s == 1 ? "HIGH" : "UNINITIALIZED";
}

/** Rack's deprecated `bool state`, a union with `State s`. Reading the bool while s is
UNINITIALIZED (2) would read an invalid bool, so read s: true unless LOW, as the byte reads. */
template <typename Trigger>
void deprecatedState(py::class_<Trigger>& c, const char* cls) {
	std::string get = std::string(cls) + ".state is deprecated in Rack; use s or is_high()";
	c.def_property("state",
		[get](const Trigger& t) {
			bindings::deprecated(get.c_str());
			return t.s != Trigger::LOW;
		},
		[get](Trigger& t, bool value) {
			bindings::deprecated(get.c_str());
			t.s = value ? Trigger::HIGH : Trigger::LOW;
		},
		"Deprecated in Rack: the state as a bool (a union with s): true unless s is LOW; assigning sets HIGH or LOW.");
}

template <typename Class>
void valueCopies(Class& c) {
	using T = typename Class::type;
	c.def("__copy__", [](const T& v) { return v; })
		.def("__deepcopy__", [](const T& v, py::dict) { return v; }, py::arg("memo"));
	c.attr("__hash__") = py::none();
}

}  // namespace

void bindDigital(py::module_ m) {
	// --- BooleanTrigger -------------------------------------------------------------
	py::class_<d::BooleanTrigger> boolean(m, "BooleanTrigger", "rack::dsp::BooleanTrigger: detects a bool turning true.");
	py::native_enum<d::BooleanTrigger::State>(boolean, "State", "enum.Enum", "BooleanTrigger::State")
		.value("LOW", d::BooleanTrigger::LOW)
		.value("HIGH", d::BooleanTrigger::HIGH)
		.value("UNINITIALIZED", d::BooleanTrigger::UNINITIALIZED)
		.finalize();
	py::native_enum<d::BooleanTrigger::Event>(boolean, "Event", "enum.Enum", "BooleanTrigger::Event")
		.value("NONE", d::BooleanTrigger::NONE)
		.value("TRIGGERED", d::BooleanTrigger::TRIGGERED)
		.value("UNTRIGGERED", d::BooleanTrigger::UNTRIGGERED)
		.finalize();
	boolean.def(py::init<>(), "s starts UNINITIALIZED, so a first true input does not trigger.")
		.def_readwrite("s", &d::BooleanTrigger::s)
		.def("reset", &d::BooleanTrigger::reset)
		.def("process", &d::BooleanTrigger::process, py::arg("in_"), "True if the input changed from false to true.")
		.def("process_event", &d::BooleanTrigger::processEvent, py::arg("in_"),
			"TRIGGERED when the input turns true, UNTRIGGERED when it turns false, else NONE.")
		.def("is_high", &d::BooleanTrigger::isHigh)
		.def("__repr__", [](const d::BooleanTrigger& t) { return "BooleanTrigger(s=" + stateName(t.s) + ")"; });
	deprecatedState(boolean, "BooleanTrigger");
	valueCopies(boolean);

	// --- TSchmittTrigger ------------------------------------------------------------
	using Schmitt = d::TSchmittTrigger<float>;
	py::class_<Schmitt> schmitt(m, "SchmittTrigger",
		"rack::dsp::SchmittTrigger (TSchmittTrigger<float>): turns HIGH at the high threshold and LOW at the low one.");
	py::native_enum<Schmitt::State>(schmitt, "State", "enum.Enum", "TSchmittTrigger<float>::State")
		.value("LOW", Schmitt::LOW)
		.value("HIGH", Schmitt::HIGH)
		.value("UNINITIALIZED", Schmitt::UNINITIALIZED)
		.finalize();
	py::native_enum<Schmitt::Event>(schmitt, "Event", "enum.Enum", "TSchmittTrigger<float>::Event")
		.value("NONE", Schmitt::NONE)
		.value("TRIGGERED", Schmitt::TRIGGERED)
		.value("UNTRIGGERED", Schmitt::UNTRIGGERED)
		.finalize();
	schmitt.def(py::init<>(), "s starts UNINITIALIZED: the first input sets the state without triggering.")
		.def_readwrite("s", &Schmitt::s)
		.def("reset", &Schmitt::reset)
		.def("process", &Schmitt::process, py::arg("in_"), py::arg("low_threshold") = 0.f, py::arg("high_threshold") = 1.f,
			"True when the input rises from LOW to at least high_threshold.")
		.def("process_event", &Schmitt::processEvent, py::arg("in_"), py::arg("low_threshold") = 0.f,
			py::arg("high_threshold") = 1.f, "TRIGGERED on a rise, UNTRIGGERED on a fall to low_threshold, else NONE.")
		.def("is_high", &Schmitt::isHigh)
		.def("__repr__", [](const Schmitt& t) { return "SchmittTrigger(s=" + stateName(t.s) + ")"; });
	deprecatedState(schmitt, "SchmittTrigger");
	valueCopies(schmitt);

	using Schmitt4 = d::TSchmittTrigger<float_4>;
	py::class_<Schmitt4> schmitt4(m, "TSchmittTrigger_float_4",
		"rack::dsp::TSchmittTrigger<simd::float_4>: four Schmitt triggers, with masks for states and results.");
	schmitt4.def(py::init<>(), "state starts all HIGH (a full mask), as Rack's generic template resets it.")
		.def_readwrite("state", &Schmitt4::state, "The state mask (a live view).")
		.def("reset", &Schmitt4::reset)
		.def("process", &Schmitt4::process, py::arg("in_"), py::arg("low_threshold") = float_4(0.f),
			py::arg("high_threshold") = float_4(1.f), "A mask of the lanes that rose from LOW to at least high_threshold.")
		.def("is_high", &Schmitt4::isHigh, "The state mask.")
		.def("__repr__", [](const Schmitt4& t) {
			return "TSchmittTrigger_float_4(state=" + py::repr(py::cast(t.state)).cast<std::string>() + ")";
		});
	valueCopies(schmitt4);

	// --- PulseGenerator -------------------------------------------------------------
	py::class_<d::PulseGenerator> pulse(m, "PulseGenerator", "rack::dsp::PulseGenerator: holds HIGH for a duration.");
	pulse.def(py::init<>())
		.def_readwrite("remaining", &d::PulseGenerator::remaining, "Seconds the pulse stays HIGH.")
		.def("reset", &d::PulseGenerator::reset, "Ends the pulse immediately.")
		.def("process", &d::PulseGenerator::process, py::arg("delta_time"),
			"Advances by delta_time seconds; returns whether the pulse was HIGH before the step.")
		.def("is_high", &d::PulseGenerator::isHigh)
		.def("trigger", &d::PulseGenerator::trigger, py::arg("duration") = 1e-3f,
			"Starts a pulse of duration seconds, unless the current one lasts longer.")
		.def("__repr__", [](const d::PulseGenerator& p) { return "PulseGenerator(remaining=" + values::floatRepr(p.remaining) + ")"; });
	valueCopies(pulse);

	// --- TTimer ---------------------------------------------------------------------
	using Timer = d::TTimer<float>;
	py::class_<Timer> timer(m, "Timer", "rack::dsp::Timer (TTimer<float>): accumulates time.");
	timer.def(py::init<>())
		.def_readwrite("time", &Timer::time)
		.def("reset", &Timer::reset)
		.def("process", &Timer::process, py::arg("delta_time"), "Adds delta_time and returns the time since reset.")
		.def("get_time", &Timer::getTime)
		.def("__repr__", [](const Timer& t) { return "Timer(time=" + values::floatRepr(t.time) + ")"; });
	valueCopies(timer);
	using Timer4 = d::TTimer<float_4>;
	py::class_<Timer4> timer4(m, "TTimer_float_4", "rack::dsp::TTimer<simd::float_4>: four timers.");
	timer4.def(py::init<>())
		.def_readwrite("time", &Timer4::time, "The times (a live view).")
		.def("reset", &Timer4::reset)
		.def("process", &Timer4::process, py::arg("delta_time"), "Adds delta_time and returns the times since reset.")
		.def("get_time", &Timer4::getTime)
		.def("__repr__", [](const Timer4& t) { return "TTimer_float_4(time=" + py::repr(py::cast(t.time)).cast<std::string>() + ")"; });
	valueCopies(timer4);

	py::object f32 = py::module_::import("builtins").attr("float");
	py::object vector = py::type::of<float_4>();
	py::dict schmitts;
	schmitts[py::make_tuple()] = schmitt;
	schmitts[py::make_tuple(f32)] = schmitt;
	schmitts[py::make_tuple(vector)] = schmitt4;
	simd_bindings::addTemplate(m, "TSchmittTrigger", schmitts);
	py::dict timers;
	timers[py::make_tuple()] = timer;
	timers[py::make_tuple(f32)] = timer;
	timers[py::make_tuple(vector)] = timer4;
	simd_bindings::addTemplate(m, "TTimer", timers);

	// --- ClockDivider ---------------------------------------------------------------
	py::class_<d::ClockDivider> divider(m, "ClockDivider", "rack::dsp::ClockDivider: true every `division` calls.");
	divider.def(py::init<>())
		.def_property("clock", [](const d::ClockDivider& c) { return c.clock; },
			[](d::ClockDivider& c, const py::int_& v) { c.clock = u32(v, "ClockDivider.clock"); })
		.def_property("division", [](const d::ClockDivider& c) { return c.division; },
			[](d::ClockDivider& c, const py::int_& v) { c.division = u32(v, "ClockDivider.division"); })
		.def("reset", &d::ClockDivider::reset)
		.def("set_division", [](d::ClockDivider& c, const py::int_& v) { c.setDivision(u32(v, "set_division")); },
			py::arg("division"))
		.def("get_division", &d::ClockDivider::getDivision)
		.def("get_clock", &d::ClockDivider::getClock)
		.def("process", &d::ClockDivider::process, "Counts a call; true (and back to 0) when the count reaches division.")
		.def("__repr__", [](const d::ClockDivider& c) {
			return "ClockDivider(clock=" + std::to_string(c.clock) + ", division=" + std::to_string(c.division) + ")";
		});
	valueCopies(divider);
}

}  // namespace dsp_bindings
}  // namespace rackpy
