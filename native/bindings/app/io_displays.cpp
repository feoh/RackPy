// SPDX-License-Identifier: GPL-3.0-or-later
// Rack's audio and MIDI displays, buttons and menus (app/AudioDisplay.hpp,
// app/MidiDisplay.hpp) and the component library's audio and MIDI buttons
// (RP-28E), with the ports Python makes (RP-29).
//
// These widgets keep a raw port pointer: their step() reads it every frame,
// and the menus they open capture it. So a Python port stays alive while any
// widget or menu that points at it does: RackPy pins the port to the widget
// (or menu) with Rack's weak pointer, and lets it go once Rack deletes it.
// Only ports Python made are accepted; a module's ports have no handle.
#include <pybind11/pybind11.h>

#include <vector>

#include <rack.hpp>

#include "../audio.hpp"
#include "../midi.hpp"
#include "app.hpp"
#include "app_internal.hpp"
#include "rackpy/runtime/interpreter.hpp"
#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace widgets {
namespace appdetail {

namespace {

namespace app = rack::app;
namespace cl = rack::componentlibrary;

struct Pin {
	rack::WeakPtr<rack::widget::Widget> widget;
	runtime::PyRef port;
};
std::vector<Pin> pins;  // main thread
bool pinHooked = false;

/** Keeps `port` (its Python object) alive while Rack keeps `widget`. */
void pin(rack::widget::Widget* widget, py::handle port) {
	pins.push_back({rack::WeakPtr<rack::widget::Widget>(widget), runtime::PyRef(py::reinterpret_borrow<py::object>(port))});
	if (pinHooked)
		return;
	pinHooked = true;
	runtime::onUiStep([] {
		if (std::none_of(pins.begin(), pins.end(), [](const Pin& p) { return !p.widget.get(); }))
			return;
		runtime::callPython("releasing ports of deleted audio/MIDI widgets", [] {
			pins.erase(std::remove_if(pins.begin(), pins.end(), [](const Pin& p) { return !p.widget.get(); }), pins.end());
		});
	});
	runtime::onContextClosing([] {
		runtime::callPython("releasing ports of audio/MIDI widgets", [] { pins.clear(); });
	});
}

// Rack's headers leave these pointers uninitialized (Rack's own code always sets them before use), so the
// widgets Python makes start with them null.
template <class Base>
struct PortWidgetInit : Widget<Base> {
	PortWidgetInit() { this->port = nullptr; }
};

struct AudioDisplayInit : Widget<app::AudioDisplay> {
	AudioDisplayInit() {
		driverChoice = nullptr;
		driverSeparator = nullptr;
		deviceChoice = nullptr;
		deviceSeparator = nullptr;
		sampleRateChoice = nullptr;
		sampleRateSeparator = nullptr;
		bufferSizeChoice = nullptr;
	}
};

struct MidiDisplayInit : Widget<app::MidiDisplay> {
	MidiDisplayInit() {
		driverChoice = nullptr;
		driverSeparator = nullptr;
		deviceChoice = nullptr;
		deviceSeparator = nullptr;
		channelChoice = nullptr;
	}
};

/** The Python port a widget's port field points at, if Python pinned it there. */
py::object pinnedPort(const rack::widget::Widget* widget, const void* port) {
	if (!port)
		return py::none();
	for (auto it = pins.rbegin(); it != pins.rend(); ++it)
		if (it->widget.get() == widget) {
			py::object o = it->port.get();
			if (py::isinstance<rack::audio::Port>(o) && &o.cast<rack::audio::Port&>() == port)
				return o;
			if (py::isinstance<rack::midi::Port>(o) && &o.cast<rack::midi::Port&>() == port)
				return o;
		}
	return py::none();
}

template <class T, class Cls>
void audioPortField(Cls& cls) {
	cls.def_property("port", [](py::object self) {
			auto& w = native<T>(self);
			return pinnedPort(&w, w.port);
		}, [](py::object self, py::object port) {
			auto& w = native<T>(self);
			if (port.is_none()) {
				w.port = nullptr;
				return;
			}
			w.port = &bindings::pythonAudioPort(port);
			pin(&w, port);
		}, "Rack's: the audio port it shows (one Python made), or None. RackPy keeps the port alive while the widget is.");
}

template <class T, class Cls>
void midiPortField(Cls& cls) {
	cls.def_property("port", [](py::object self) {
			auto& w = native<T>(self);
			return pinnedPort(&w, w.port);
		}, [](py::object self, py::object port) {
			auto& w = native<T>(self);
			if (port.is_none()) {
				w.port = nullptr;
				return;
			}
			w.port = &bindings::pythonMidiPort(port);
			pin(&w, port);
		}, "Rack's: the MIDI port it shows (one Python made), or None. RackPy keeps the port alive while the widget is.");
}

}  // namespace

void bindIoDisplays(py::module_ raw, py::module_ mod) {
	using LC = app::LedDisplayChoice;
	auto audioDriver = variant<app::AudioDriverChoice, LC, PortWidgetInit<app::AudioDriverChoice>>(mod, "AudioDriverChoice");
	audioPortField<app::AudioDriverChoice>(audioDriver);
	auto audioDevice = variant<app::AudioDeviceChoice, LC, PortWidgetInit<app::AudioDeviceChoice>>(mod, "AudioDeviceChoice");
	audioPortField<app::AudioDeviceChoice>(audioDevice);
	auto audioRate = variant<app::AudioSampleRateChoice, LC, PortWidgetInit<app::AudioSampleRateChoice>>(mod, "AudioSampleRateChoice");
	audioPortField<app::AudioSampleRateChoice>(audioRate);
	auto audioBlock = variant<app::AudioBlockSizeChoice, LC, PortWidgetInit<app::AudioBlockSizeChoice>>(mod, "AudioBlockSizeChoice");
	audioPortField<app::AudioBlockSizeChoice>(audioBlock);
	variant<app::AudioDeviceMenuChoice, app::AudioDeviceChoice, PortWidgetInit<app::AudioDeviceMenuChoice>>(mod, "AudioDeviceMenuChoice");

	using AD = app::AudioDisplay;
	variant<AD, app::LedDisplay, AudioDisplayInit>(mod, "AudioDisplay")
		.def("set_audio_port", [](py::object self, py::object port) {
			checkMutation();
			auto& w = native<AD>(self);
			rack::audio::Port* p = port.is_none() ? nullptr : &bindings::pythonAudioPort(port);
			w.setAudioPort(p);  // Rack's: rebuilds its children for the port
			if (p)
				pin(&w, port);
		}, py::arg("port"), "Rack's: rebuilds the display's choices (Rack's widgets) for the port, which RackPy keeps alive "
			"while the display is. Size the display first: its choices take its width.")
		.def_property_readonly("driver_choice", [](py::object self) { return wrapper(native<AD>(self).driverChoice); })
		.def_property_readonly("driver_separator", [](py::object self) { return wrapper(native<AD>(self).driverSeparator); })
		.def_property_readonly("device_choice", [](py::object self) { return wrapper(native<AD>(self).deviceChoice); })
		.def_property_readonly("device_separator", [](py::object self) { return wrapper(native<AD>(self).deviceSeparator); })
		.def_property_readonly("sample_rate_choice", [](py::object self) { return wrapper(native<AD>(self).sampleRateChoice); })
		.def_property_readonly("sample_rate_separator",
			[](py::object self) { return wrapper(native<AD>(self).sampleRateSeparator); })
		.def_property_readonly("buffer_size_choice", [](py::object self) { return wrapper(native<AD>(self).bufferSizeChoice); });

	using AB = app::AudioButton;
	auto audioButton = variant<AB, app::SvgButton, PortWidgetInit<AB>>(mod, "AudioButton");
	audioPortField<AB>(audioButton);
	audioButton.def("set_audio_port", [](py::object self, py::object port) {
		auto& w = native<AB>(self);
		rack::audio::Port* p = port.is_none() ? nullptr : &bindings::pythonAudioPort(port);
		w.setAudioPort(p);
		if (p)
			pin(&w, port);
	}, py::arg("port"), "Rack's: the port whose menu a click opens; RackPy keeps it alive while the button is.");

	auto midiDriver = variant<app::MidiDriverChoice, LC, PortWidgetInit<app::MidiDriverChoice>>(mod, "MidiDriverChoice");
	midiPortField<app::MidiDriverChoice>(midiDriver);
	auto midiDevice = variant<app::MidiDeviceChoice, LC, PortWidgetInit<app::MidiDeviceChoice>>(mod, "MidiDeviceChoice");
	midiPortField<app::MidiDeviceChoice>(midiDevice);
	auto midiChannel = variant<app::MidiChannelChoice, LC, PortWidgetInit<app::MidiChannelChoice>>(mod, "MidiChannelChoice");
	midiPortField<app::MidiChannelChoice>(midiChannel);

	using MD = app::MidiDisplay;
	variant<MD, app::LedDisplay, MidiDisplayInit>(mod, "MidiDisplay")
		.def("set_midi_port", [](py::object self, py::object port) {
			checkMutation();
			auto& w = native<MD>(self);
			rack::midi::Port* p = port.is_none() ? nullptr : &bindings::pythonMidiPort(port);
			w.setMidiPort(p);
			if (p)
				pin(&w, port);
		}, py::arg("port"), "Rack's: rebuilds the display's choices for the port, which RackPy keeps alive while the "
			"display is.")
		.def_property_readonly("driver_choice", [](py::object self) { return wrapper(native<MD>(self).driverChoice); })
		.def_property_readonly("driver_separator", [](py::object self) { return wrapper(native<MD>(self).driverSeparator); })
		.def_property_readonly("device_choice", [](py::object self) { return wrapper(native<MD>(self).deviceChoice); })
		.def_property_readonly("device_separator", [](py::object self) { return wrapper(native<MD>(self).deviceSeparator); })
		.def_property_readonly("channel_choice", [](py::object self) { return wrapper(native<MD>(self).channelChoice); });

	using MB = app::MidiButton;
	auto midiButton = variant<MB, app::SvgButton, PortWidgetInit<MB>>(mod, "MidiButton");
	midiPortField<MB>(midiButton);
	midiButton.def("set_midi_port", [](py::object self, py::object port) {
		auto& w = native<MB>(self);
		rack::midi::Port* p = port.is_none() ? nullptr : &bindings::pythonMidiPort(port);
		w.setMidiPort(p);
		if (p)
			pin(&w, port);
	}, py::arg("port"), "Rack's: the port whose menu a click opens; RackPy keeps it alive while the button is.");

	mod.def("append_audio_menu", [](py::object menu, py::object port) {
		auto* m = menuArgument(menu);
		checkMutationOf(*m);
		auto& p = bindings::pythonAudioPort(port);
		pin(m, port);  // the menu's items capture the port
		app::appendAudioMenu(m, &p);
	}, py::arg("menu"), py::arg("port"), "Rack's: driver, device, sample rate and block size items for the port, which "
		"RackPy keeps alive while the menu is.");
	mod.def("append_midi_menu", [](py::object menu, py::object port) {
		auto* m = menuArgument(menu);
		checkMutationOf(*m);
		auto& p = bindings::pythonMidiPort(port);
		pin(m, port);
		app::appendMidiMenu(m, &p);
	}, py::arg("menu"), py::arg("port"), "Rack's: driver, device and channel items for the port, which RackPy keeps "
		"alive while the menu is.");

	// The component library's audio and MIDI buttons: Rack's classes, with Rack's artwork.
	py::module_ library = raw.attr("componentlibrary").cast<py::module_>();
	variant<cl::AudioButton_ADAT, AB, PortWidgetInit<cl::AudioButton_ADAT>>(library, "AudioButton_ADAT");
	variant<cl::AudioButton_USB_B, AB, PortWidgetInit<cl::AudioButton_USB_B>>(library, "AudioButton_USB_B");
	variant<cl::MidiButton_MIDI_DIN, MB, PortWidgetInit<cl::MidiButton_MIDI_DIN>>(library, "MidiButton_MIDI_DIN");
}

}  // namespace appdetail
}  // namespace widgets
}  // namespace rackpy
