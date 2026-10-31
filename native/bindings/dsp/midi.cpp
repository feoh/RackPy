// SPDX-License-Identifier: GPL-3.0-or-later
// dsp/vumeter.hpp and dsp/midi.hpp -> rackpy.raw.dsp (RP-18D): Rack's VU meters
// and its MIDI generator and parser, as stateful native Python objects.
//
// Each method calls Rack's inline code through calls.hpp's out-of-line member
// wrapper, as the C++ reference does. MidiGenerator<CHANNELS> and
// MidiParser<MAX_CHANNELS> are instantiated for 1 to 16 channels (Rack's port
// and MIDI channel limit): MidiGenerator[16] is MidiGenerator_16, the others
// live in rackpy.raw.dsp._midi.
//
// MidiGenerator::onMessage is a virtual Rack calls with each message it
// generates. Its RP-06 strategy is "engine" (ADR 003), so outside the
// callback laboratory Python cannot override it; RackPy's generator
// implements it natively by recording the messages, which take_messages()
// returns. A rackpy.laboratory.MidiGenerator[N] subclass may override
// on_message (RP-22E): Rack's code calls it synchronously, and super()
// records (through _on_message), as RackPy's native implementation does.
//
// Rack's MidiParser trusts its inputs: an out-of-range channel index, an MPE
// message on a channel above MAX_CHANNELS, more channels than MAX_CHANNELS, a
// timing clock with clockDivision 0, or patch JSON with out-of-range values
// would read or write outside its arrays, divide by zero or make an invalid
// enum. RackPy refuses those first (ValueError or IndexError).
#include "dsp.hpp"

#include <pybind11/native_enum.h>
#include <pybind11/stl.h>

#include <string>
#include <utility>
#include <vector>

#include "../../laboratory/laboratory.hpp"
#include "../simd/simd.hpp"
#include "../support/overrides.hpp"
#include "../values/values.hpp"
#include "arrays.hpp"
#include "calls.hpp"
#include "rackpy/runtime/json.hpp"

namespace rackpy {
namespace dsp_bindings {

namespace d = rack::dsp;
namespace c = dsp_calls;
using rack::midi::Message;

namespace {

template <typename I>
I integer(const py::int_& v, const char* what, const char* type) {
	return values::toInt<I>(v, what, type);
}

int index(const py::int_& v, int size, const char* what) {
	int i = values::toInt<int>(v, what, "int");
	if (i < 0 || i >= size)
		throw py::index_error(std::string(what) + ": the index must be 0 to " + std::to_string(size - 1) + ", not " + std::to_string(i));
	return i;
}

// --- VU meters ----------------------------------------------------------------------

void bindVuMeters(py::module_ m) {
	using Old = d::VuMeter;
	py::class_<Old> old(m, "VuMeter", "Deprecated in Rack (use VuMeter2): a VU meter's lights from a value.");
	old.def(py::init([] {
		bindings::deprecated("rackpy.raw.dsp.VuMeter is deprecated in Rack; use VuMeter2");
		Old v;
		v.dBScaled = 0.f;  // Rack leaves it uninitialized until set_value()
		return v;
	}))
		.def_readwrite("db_interval", &Old::dBInterval, "Decibels between adjacent lights (dBInterval).")
		.def_readwrite("db_scaled", &Old::dBScaled, "The value in dB divided by db_interval (dBScaled; 0 before set_value).")
		.def("set_value", [](Old& v, float value) { c::member<&Old::setValue>(v, value); }, py::arg("v"),
			"Sets the level; 1 is clipping.")
		.def("get_brightness", [](Old& v, int i) { return c::member<&Old::getBrightness>(v, i); }, py::arg("i"),
			"Light 0 is the clip light (0 or 1); light i is fully on at -db_interval * i and above.");
	valueCopies(old);

	using Meter = d::VuMeter2;
	py::class_<Meter> meter(m, "VuMeter2", "rack::dsp::VuMeter2: a smoothed peak or RMS VU meter.");
	py::native_enum<Meter::Mode>(meter, "Mode", "enum.Enum", "VuMeter2::Mode")
		.value("PEAK", Meter::PEAK)
		.value("RMS", Meter::RMS)
		.finalize();
	meter.def(py::init<>())
		.def_readwrite("mode", &Meter::mode)
		.def_readwrite("v", &Meter::v, "The smoothed peak, or the mean square, by mode.")
		.def_readwrite("lambda_", &Meter::lambda, "Rack's lambda (a Python keyword): the inverse time constant in 1/s.")
		.def("reset", [](Meter& v) { c::member<&Meter::reset>(v); })
		.def("process", [](Meter& v, float dt, float value) { c::member<&Meter::process>(v, dt, value); }, py::arg("delta_time"),
			py::arg("value"), "Updates the meter with one frame.")
		.def("get_brightness", [](Meter& v, float lo, float hi) { return c::member<&Meter::getBrightness>(v, lo, hi); },
			py::arg("db_min"), py::arg("db_max"), "0 at db_min and below, 1 at db_max and above, linear in dB between.");
	valueCopies(meter);
}

// --- MidiGenerator --------------------------------------------------------------

/** Rack's generator with onMessage implemented natively: it records each message. */
template <int N>
struct RecordingGenerator : d::MidiGenerator<N> {
	std::vector<Message> sent;
	bool laboratoryCallbacks = false;
	// pybind11 deletes a Python-made generator (a PyRecordingGenerator) through this type; Rack's
	// MidiGenerator has virtual functions but no virtual destructor.
	virtual ~RecordingGenerator() = default;
	void onMessage(const Message& message) override { sent.push_back(message); }
};

/** A Python subclass's on_message (rackpy.laboratory.MidiGenerator only: rackpy.raw.dsp
refuses others at definition). Python reaches the generator only from its own calls; a call
without the GIL goes through callPython, which never runs Python on Rack's threads. */
template <int N>
struct PyRecordingGenerator : RecordingGenerator<N> {
	using R = RecordingGenerator<N>;
	void onMessage(const Message& message) override {
		if (!this->laboratoryCallbacks) {
			R::onMessage(message);
			return;
		}
		bindings::overrideOrVoid(static_cast<const R*>(this), "on_message", [this, &message] { R::onMessage(message); },
			Message(message));  // Python may retain it after Rack's stack-local message is gone.
	}
};

template <int N>
py::object bindGenerator(py::module_ scope) {
	using G = d::MidiGenerator<N>;
	using R = RecordingGenerator<N>;
	std::string name = "MidiGenerator_" + std::to_string(N);
	py::class_<R, PyRecordingGenerator<N>> cls(scope, name.c_str(), ("rack::dsp::MidiGenerator<" + std::to_string(N) +
		">: turns gates and CV into MIDI messages; take_messages() returns the messages it generated.").c_str());
	cls.attr("CHANNELS") = N;
	auto base = [](R& r) -> G& { return r; };
	cls.def(py::init<>())
		.def_property("channels", [](const R& r) { return (int) r.channels; }, [](R& r, const py::int_& v) {
			int n = integer<int>(v, "MidiGenerator.channels", "uint8_t");
			if (n < 0 || n > N)
				throw py::value_error("channels must be 0 to " + std::to_string(N) + " (CHANNELS)");
			r.channels = (uint8_t) n;
		})
		.def_property_readonly("vels", [](py::object self) { return view(self, self.cast<R&>().vels, N); })
		.def_property_readonly("notes", [](py::object self) { return view(self, self.cast<R&>().notes, N); })
		.def_property_readonly("gates", [](py::object self) { return view(self, self.cast<R&>().gates, N); })
		.def_property_readonly("key_pressures", [](py::object self) { return view(self, self.cast<R&>().keyPressures, N); })
		.def_property("channel_pressure", [](const R& r) { return (int) r.channelPressure; },
			[](R& r, const py::int_& v) { r.channelPressure = integer<int8_t>(v, "channel_pressure", "int8_t"); })
		.def_property_readonly("ccs", [](py::object self) { return view(self, self.cast<R&>().ccs, 128); })
		.def_property("pw", [](const R& r) { return (int) r.pw; }, [](R& r, const py::int_& v) { r.pw = integer<int16_t>(v, "pw", "int16_t"); })
		.def_readwrite("clk", &R::clk)
		.def_readwrite("start", &R::start)
		.def_readwrite("stop", &R::stop)
		.def_readwrite("cont", &R::cont)
		.def_readwrite("frame", &R::frame)
		.def("reset", [base](R& r) { c::member<&G::reset>(base(r)); })
		.def("panic", [base](R& r) { c::member<&G::panic>(base(r)); }, "Resets, and sends note off for every note.")
		.def("set_channels", [base](R& r, const py::int_& v) {
			int n = integer<int>(v, "set_channels", "uint8_t");
			if (n < 0 || n > N)
				throw py::value_error("set_channels: channels must be 0 to " + std::to_string(N) + "; Rack would index past its arrays");
			c::member<&G::setChannels>(base(r), (uint8_t) n);
		}, py::arg("channels"))
		.def("set_velocity", [base](R& r, const py::int_& vel, const py::int_& ch) {
			c::member<&G::setVelocity>(base(r), integer<int8_t>(vel, "set_velocity", "int8_t"), index(ch, N, "set_velocity"));
		}, py::arg("vel"), py::arg("c"), "Call before set_note_gate.")
		.def("set_note_gate", [base](R& r, const py::int_& note, bool gate, const py::int_& ch) {
			c::member<&G::setNoteGate>(base(r), integer<int8_t>(note, "set_note_gate", "int8_t"), gate, index(ch, N, "set_note_gate"));
		}, py::arg("note"), py::arg("gate"), py::arg("c"))
		.def("set_key_pressure", [base](R& r, const py::int_& val, const py::int_& ch) {
			c::member<&G::setKeyPressure>(base(r), integer<int8_t>(val, "set_key_pressure", "int8_t"), index(ch, N, "set_key_pressure"));
		}, py::arg("val"), py::arg("c"))
		.def("set_channel_pressure", [base](R& r, const py::int_& val) {
			c::member<&G::setChannelPressure>(base(r), integer<int8_t>(val, "set_channel_pressure", "int8_t"));
		}, py::arg("val"))
		.def("set_cc", [base](R& r, const py::int_& cc, const py::int_& id) {
			c::member<&G::setCc>(base(r), integer<int8_t>(cc, "set_cc", "int8_t"), index(id, 128, "set_cc"));
		}, py::arg("cc"), py::arg("id"))
		.def("set_mod_wheel", [base](R& r, const py::int_& cc) { c::member<&G::setModWheel>(base(r), integer<int8_t>(cc, "set_mod_wheel", "int8_t")); }, py::arg("cc"))
		.def("set_volume", [base](R& r, const py::int_& cc) { c::member<&G::setVolume>(base(r), integer<int8_t>(cc, "set_volume", "int8_t")); }, py::arg("cc"))
		.def("set_balance", [base](R& r, const py::int_& cc) { c::member<&G::setBalance>(base(r), integer<int8_t>(cc, "set_balance", "int8_t")); }, py::arg("cc"))
		.def("set_pan", [base](R& r, const py::int_& cc) { c::member<&G::setPan>(base(r), integer<int8_t>(cc, "set_pan", "int8_t")); }, py::arg("cc"))
		.def("set_sustain_pedal", [base](R& r, const py::int_& cc) {
			c::member<&G::setSustainPedal>(base(r), integer<int8_t>(cc, "set_sustain_pedal", "int8_t"));
		}, py::arg("cc"))
		.def("set_pitch_wheel", [base](R& r, const py::int_& pw) {
			c::member<&G::setPitchWheel>(base(r), integer<int16_t>(pw, "set_pitch_wheel", "int16_t"));
		}, py::arg("pw"))
		.def("set_clock", [base](R& r, bool v) { c::member<&G::setClock>(base(r), v); }, py::arg("clk"))
		.def("set_start", [base](R& r, bool v) { c::member<&G::setStart>(base(r), v); }, py::arg("start"))
		.def("set_continue", [base](R& r, bool v) { c::member<&G::setContinue>(base(r), v); }, py::arg("cont"))
		.def("set_stop", [base](R& r, bool v) { c::member<&G::setStop>(base(r), v); }, py::arg("stop"))
		.def("set_frame", [base](R& r, int64_t frame) { c::member<&G::setFrame>(base(r), frame); }, py::arg("frame"))
		.def("take_messages", [](R& r) {
			std::vector<Message> out;
			out.swap(r.sent);
			return out;
		}, "The messages generated since the last call (Rack's onMessage, implemented natively), oldest first.")
		// Not public: an ordinary generator has no on_message (RP-18D). The laboratory's
		// MidiGenerator[N].on_message calls this as its superclass implementation.
		.def("_enable_laboratory", [](R& r) {
			laboratory::requireEnabled("creating a laboratory MIDI generator");
			r.laboratoryCallbacks = true;
		})
		.def("_on_message", [](R& r, const Message& message) { r.R::onMessage(message); }, py::arg("message"),
			"RackPy's onMessage, non-virtually: records the message for take_messages().");
	valueCopies(cls);
	return cls;
}

// --- MidiParser -----------------------------------------------------------------

template <int N>
struct ParserGuards {
	using P = d::MidiParser<N>;
	/** True if Rack would index its arrays with the message's MIDI channel (MPE, polyphonic). */
	static bool usesChannel(const P& p, const Message& msg) {
		if (!(p.channels > 1 && p.polyMode == P::MPE_MODE))
			return false;
		switch (msg.getStatus()) {
			case 0x8:
			case 0x9:
			case 0xd:
			case 0xe: return true;
			case 0xb: return msg.getNote() == 0x01;
			default: return false;
		}
	}
	static void check(const P& p, const Message& msg, const char* what) {
		if (usesChannel(p, msg) && msg.getChannel() >= N)
			throw py::value_error(std::string(what) + ": in MPE mode Rack indexes its " + std::to_string(N) +
				"-channel arrays by the message's MIDI channel " + std::to_string(msg.getChannel()));
		if (msg.getStatus() == 0xf && msg.getChannel() == 0x8 && p.clockDivision == 0)
			throw py::value_error(std::string(what) + ": clock_division is 0, and Rack would divide by it");
	}
	static void mpeChannel(const P& p, int channel, const char* what) {
		if (p.channels > 1 && p.polyMode == P::MPE_MODE && channel >= N)
			throw py::index_error(std::string(what) + ": in MPE mode the channel must be below " + std::to_string(N));
	}
};

template <typename E>
void checkJsonEnum(py::dict json, const char* key, int count) {
	if (!json.contains(key))
		return;
	py::object v = json[key];
	if (py::isinstance<py::int_>(v) && !py::isinstance<py::bool_>(v)) {
		long long x = v.cast<long long>();
		if (x < 0 || x > count)
			throw py::value_error(std::string("from_json: ") + key + " is " + std::to_string(x) + "; Rack would make an invalid enum");
	}
}

template <int N>
py::object bindParser(py::module_ scope, py::object monoModes, py::object polyModes) {
	using P = d::MidiParser<N>;
	using Guard = ParserGuards<N>;
	std::string name = "MidiParser_" + std::to_string(N);
	py::class_<P> cls(scope, name.c_str(), ("rack::dsp::MidiParser<" + std::to_string(N) +
		">: turns MIDI messages into gates, CV and pulses.").c_str());
	cls.attr("MAX_CHANNELS") = N;
	if (monoModes.is_none()) {
		py::native_enum<typename P::MonoMode>(cls, "MonoMode", "enum.Enum", "MidiParser::MonoMode")
			.value("LAST_PRIORITY_MODE", P::LAST_PRIORITY_MODE)
			.value("FIRST_PRIORITY_MODE", P::FIRST_PRIORITY_MODE)
			.value("LOWEST_PRIORITY_MODE", P::LOWEST_PRIORITY_MODE)
			.value("HIGHEST_PRIORITY_MODE", P::HIGHEST_PRIORITY_MODE)
			.value("NUM_MONO_MODES", P::NUM_MONO_MODES)
			.finalize();
		py::native_enum<typename P::PolyMode>(cls, "PolyMode", "enum.Enum", "MidiParser::PolyMode")
			.value("ROTATE_MODE", P::ROTATE_MODE)
			.value("REUSE_MODE", P::REUSE_MODE)
			.value("RESET_MODE", P::RESET_MODE)
			.value("MPE_MODE", P::MPE_MODE)
			.value("NUM_POLY_MODES", P::NUM_POLY_MODES)
			.finalize();
	}
	else {
		cls.attr("MonoMode") = monoModes;
		cls.attr("PolyMode") = polyModes;
	}
	// One Python enum for every instantiation (C++ has one per instantiation, with the same values): convert by value.
	using Mono16 = typename d::MidiParser<16>::MonoMode;
	using Poly16 = typename d::MidiParser<16>::PolyMode;
	cls.def(py::init<>())
		.def_property("channels", [](const P& p) { return (int) p.channels; }, [](P& p, const py::int_& v) {
			int n = integer<int>(v, "MidiParser.channels", "uint8_t");
			if (n < 0 || n > N)
				throw py::value_error("channels must be 0 to " + std::to_string(N) + " (MAX_CHANNELS)");
			p.channels = (uint8_t) n;
		})
		.def_property("mono_mode", [](const P& p) { return (Mono16) p.monoMode; }, [](P& p, Mono16 v) { p.monoMode = (typename P::MonoMode) v; })
		.def_readwrite("retrigger_on_resume", &P::retriggerOnResume)
		.def_property("poly_mode", [](const P& p) { return (Poly16) p.polyMode; }, [](P& p, Poly16 v) { p.polyMode = (typename P::PolyMode) v; })
		.def_readwrite("release_velocity_enabled", &P::releaseVelocityEnabled)
		.def_readwrite("pw_range", &P::pwRange)
		.def_readwrite("smooth", &P::smooth)
		.def_property("clock_division", [](const P& p) { return p.clockDivision; },
			[](P& p, const py::int_& v) { p.clockDivision = integer<uint32_t>(v, "clock_division", "uint32_t"); },
			"24 PPQN clocks per divider pulse. 0 is stored, but a timing clock message is then refused.")
		.def_readwrite("clock", &P::clock)
		.def_readwrite("pedal", &P::pedal)
		.def_property_readonly("notes", [](py::object self) { return view(self, self.cast<P&>().notes, N); })
		.def_property_readonly("gates", [](py::object self) { return view(self, self.cast<P&>().gates, N); })
		.def_property_readonly("velocities", [](py::object self) { return view(self, self.cast<P&>().velocities, N); })
		.def_property_readonly("aftertouches", [](py::object self) { return view(self, self.cast<P&>().aftertouches, N); })
		.def_property("held_notes", [](const P& p) { return std::vector<int>(p.heldNotes.begin(), p.heldNotes.end()); },
			[](P& p, const std::vector<py::int_>& v) {
				std::vector<uint8_t> held;
				for (const auto& x : v)
					held.push_back(integer<uint8_t>(x, "held_notes", "uint8_t"));
				p.heldNotes = std::move(held);
			}, "A copy, as a list of ints; assigning replaces it.")
		.def_property("rotate_index", [](const P& p) { return (int) p.rotateIndex; },
			[](P& p, const py::int_& v) { p.rotateIndex = integer<int8_t>(v, "rotate_index", "int8_t"); })
		.def_property_readonly("pws", [](py::object self) { return view(self, self.cast<P&>().pws, N); }, "Pitch wheels, -8192 to 8191.")
		.def_property_readonly("mods", [](py::object self) { return view(self, self.cast<P&>().mods, N); }, "Mod wheels, 0 to 127.")
		.def_property_readonly("pw_filters", [](py::object self) { return view(self, self.cast<P&>().pwFilters, N); })
		.def_property_readonly("mod_filters", [](py::object self) { return view(self, self.cast<P&>().modFilters, N); })
		.def_readwrite("clock_pulse", &P::clockPulse)
		.def_readwrite("clock_divider_pulse", &P::clockDividerPulse)
		.def_property_readonly("retrigger_pulses", [](py::object self) { return view(self, self.cast<P&>().retriggerPulses, N); })
		.def_readwrite("start_pulse", &P::startPulse)
		.def_readwrite("stop_pulse", &P::stopPulse)
		.def_readwrite("continue_pulse", &P::continuePulse)
		.def("reset", [](P& p) { c::member<&P::reset>(p); }, "Resets settings and performance state.")
		.def("panic", [](P& p) { c::member<&P::panic>(p); }, "Resets performance state.")
		.def("process_filters", [](P& p, float dt) { c::member<&P::processFilters>(p, dt); }, py::arg("delta_time"))
		.def("process_pulses", [](P& p, float dt) { c::member<&P::processPulses>(p, dt); }, py::arg("delta_time"))
		.def("process_message", [](P& p, const Message& msg) {
			Guard::check(p, msg, "process_message");
			c::member<&P::processMessage>(p, msg);
		}, py::arg("msg"))
		.def("process_cc", [](P& p, const Message& msg) {
			Guard::check(p, msg, "process_cc");
			c::member<&P::processCC>(p, msg);
		}, py::arg("msg"))
		.def("process_system", [](P& p, const Message& msg) {
			Guard::check(p, msg, "process_system");
			c::member<&P::processSystem>(p, msg);
		}, py::arg("msg"))
		.def("assign_channel", [](P& p, const py::int_& note) { return c::member<&P::assignChannel>(p, integer<uint8_t>(note, "assign_channel", "uint8_t")); },
			py::arg("note"))
		.def("press_note", [](P& p, const py::int_& note, const py::int_& channel, const py::int_& velocity) {
			uint8_t ch = integer<uint8_t>(channel, "press_note", "uint8_t");
			Guard::mpeChannel(p, ch, "press_note");
			c::member<&P::pressNote>(p, integer<uint8_t>(note, "press_note", "uint8_t"), ch, integer<uint8_t>(velocity, "press_note", "uint8_t"));
		}, py::arg("note"), py::arg("channel"), py::arg("velocity"))
		.def("release_note", [](P& p, const py::int_& note, const py::int_& channel, const py::int_& velocity) {
			int8_t ch = integer<int8_t>(channel, "release_note", "int8_t");
			Guard::mpeChannel(p, ch, "release_note");
			c::member<&P::releaseNote>(p, integer<uint8_t>(note, "release_note", "uint8_t"), ch,
				integer<int8_t>(velocity, "release_note", "int8_t"));
		}, py::arg("note"), py::arg("channel"), py::arg("velocity"), "velocity -1 means unset.")
		.def("refresh_held", [](P& p) { c::member<&P::refreshHeld>(p); })
		.def("press_pedal", [](P& p) { c::member<&P::pressPedal>(p); })
		.def("release_pedal", [](P& p) { c::member<&P::releasePedal>(p); })
		.def("get_channels", [](P& p) { return c::member<&P::getChannels>(p); })
		.def("set_channels", [](P& p, const py::int_& v) {
			int n = integer<int>(v, "set_channels", "uint8_t");
			if (n < 0 || n > N)
				throw py::value_error("set_channels: channels must be 0 to " + std::to_string(N) + "; Rack would index past its arrays");
			c::member<&P::setChannels>(p, (uint8_t) n);
		}, py::arg("channels"))
		.def("set_mono_mode", [](P& p, Mono16 v) { c::member<&P::setMonoMode>(p, (typename P::MonoMode) v); }, py::arg("mono_mode"))
		.def("set_poly_mode", [](P& p, Poly16 v) { c::member<&P::setPolyMode>(p, (typename P::PolyMode) v); }, py::arg("poly_mode"))
		.def("get_pitch_voltage", [](P& p, const py::int_& channel) {
			return c::member<&P::getPitchVoltage>(p, (uint8_t) index(channel, N, "get_pitch_voltage"));
		}, py::arg("channel"), "The channel's note plus pitch wheel, in V/oct (0 V is C4).")
		.def("set_filter_lambda", [](P& p, float lambda) { c::member<&P::setFilterLambda>(p, lambda); }, py::arg("lambda_"))
		.def("get_pw", [](P& p, const py::int_& channel) { return c::member<&P::getPw>(p, (uint8_t) index(channel, N, "get_pw")); },
			py::arg("channel"), "The smoothed pitch wheel, -1 to 1.")
		.def("get_mod", [](P& p, const py::int_& channel) { return c::member<&P::getMod>(p, (uint8_t) index(channel, N, "get_mod")); },
			py::arg("channel"), "The smoothed mod wheel, 0 to 1.")
		.def("get_wheel_channels", [](P& p) { return c::member<&P::getWheelChannels>(p); })
		.def("to_json", [](P& p) {
			json_t* json = c::member<&P::toJson>(p);
			py::object out = runtime::fromJson(json);
			json_decref(json);
			return out;
		}, "The settings as Python values (a dict), as Rack saves them.")
		.def("from_json", [](P& p, py::dict json) {
			// Patch JSON is untrusted: refuse what Rack would turn into out-of-range indices or enums.
			if (json.contains("channels") && py::isinstance<py::int_>(json["channels"]) && !py::isinstance<py::bool_>(json["channels"])) {
				long long n = json["channels"].cast<long long>();
				if (n < 0 || n > N)
					throw py::value_error("from_json: channels is " + std::to_string(n) + "; it must be 0 to " + std::to_string(N));
			}
			checkJsonEnum<typename P::MonoMode>(json, "monoMode", P::NUM_MONO_MODES);
			checkJsonEnum<typename P::PolyMode>(json, "polyMode", P::NUM_POLY_MODES);
			json_t* j = runtime::toJson(json);
			try {
				c::member<&P::fromJson>(p, j);
			}
			catch (...) {
				json_decref(j);
				throw;
			}
			json_decref(j);
		}, py::arg("json"), "Restores settings saved by to_json (Rack's keys). Out-of-range channels or modes raise ValueError.");
	valueCopies(cls);
	return cls;
}

template <int... I>
void bindGenerators(py::module_ scope, py::dict& specs, py::module_ dsp, std::integer_sequence<int, I...>) {
	((specs[py::make_tuple(I + 1)] = bindGenerator<I + 1>(scope)), ...);
	dsp.attr("MidiGenerator_16") = specs[py::make_tuple(16)];
}

template <int N>
void addParser(py::module_ scope, py::dict& specs, py::object p16) {
	if constexpr (N != 16)
		specs[py::make_tuple(N)] = bindParser<N>(scope, p16.attr("MonoMode"), p16.attr("PolyMode"));
}

template <int... I>
void bindParsers(py::module_ scope, py::dict& specs, py::module_ dsp, std::integer_sequence<int, I...>) {
	// MidiParser<16> first: its enums are every instantiation's.
	py::object p16 = bindParser<16>(scope, py::none(), py::none());
	specs[py::make_tuple(16)] = p16;
	(addParser<I + 1>(scope, specs, p16), ...);
	dsp.attr("MidiParser_16") = p16;
}

}  // namespace

#ifdef RACKPY_TEST_HOOKS
rack::dsp::MidiGenerator<16>& midiGenerator16ForTesting(py::handle value) {
	return value.cast<RecordingGenerator<16>&>();
}
#endif

void bindMidi(py::module_ m) {
	bindVuMeters(m);
	bindArrayView<int8_t>(m, "Int8Array");
	bindArrayView<uint8_t>(m, "UInt8Array");
	bindArrayView<int16_t>(m, "Int16Array");
	bindArrayView<bool>(m, "BoolArray");
	bindArrayView<d::ExponentialFilter>(m, "ExponentialFilterArray");
	bindArrayView<d::PulseGenerator>(m, "PulseGeneratorArray");
	py::module_ scope = m.def_submodule("_midi", "MidiGenerator's and MidiParser's instantiations; reach them as MidiGenerator[N].");
	py::dict generators, parsers;
	bindGenerators(scope, generators, m, std::make_integer_sequence<int, 16>());
	bindParsers(scope, parsers, m, std::make_integer_sequence<int, 16>());
	simd_bindings::addTemplate(m, "MidiGenerator", generators);
	simd_bindings::addTemplate(m, "MidiParser", parsers);
}

}  // namespace dsp_bindings
}  // namespace rackpy
