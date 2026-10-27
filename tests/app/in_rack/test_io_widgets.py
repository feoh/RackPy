# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-28E: Rack's audio and MIDI displays, buttons and menus with Python's ports, and the rack's containers, in
Rack's live UI (Rack's audio drivers are isolated in tests and list no devices)."""

import gc

import pytest

from rackpy import raw
from rackpy.errors import OwnershipError
from rackpy.raw import app, audio, componentlibrary, midi, ui
from rackpy.raw import widget as w
from rackpy.raw.math import Vec

LOOPBACK = -12


def rack() -> app.HostRackWidget:
    context = raw.context_get()
    assert context is not None and context.scene is not None
    return context.scene.rack


def test_choices_show_their_ports() -> None:
    port = audio.Port()
    choice = app.AudioDriverChoice()
    choice.box.size = Vec(100, 20)
    assert choice.port is None
    choice.step()  # Rack's: no port, no driver
    assert "(" in choice.text
    choice.port = port
    assert choice.port is port
    choice.step()  # Rack's step reads the port's driver
    assert choice.text == port.get_driver_name()
    for audio_cls in (
        app.AudioDeviceChoice,
        app.AudioSampleRateChoice,
        app.AudioBlockSizeChoice,
        app.AudioDeviceMenuChoice,
    ):
        other = audio_cls()
        other.port = port
        other.step()
        assert isinstance(other.text, str)
    loop = midi.InputQueue()
    loop.set_driver_id(LOOPBACK)
    loop.set_device_id(2)
    names = []
    for midi_cls in (app.MidiDriverChoice, app.MidiDeviceChoice, app.MidiChannelChoice):
        c = midi_cls()
        c.box.size = Vec(100, 20)
        c.port = loop
        c.step()
        names.append(c.text)
    assert names[0] == "Loopback" and names[1] == "Loopback 3"
    with pytest.raises(TypeError):
        choice.port = loop  # type: ignore[assignment]  # an audio choice takes an audio port


def test_displays_rebuild_for_a_port_and_keep_it_alive() -> None:
    display = app.AudioDisplay()
    display.box.size = Vec(130, 90)
    display.set_audio_port(audio.Port())  # the only reference: the display keeps it
    gc.collect()
    assert display.driver_choice is not None and display.device_choice is not None
    assert display.sample_rate_choice is not None and display.buffer_size_choice is not None
    assert display.driver_separator is not None and display.device_separator is not None
    assert display.sample_rate_separator is not None
    audio_parts = [
        display.driver_choice,
        display.driver_separator,
        display.device_choice,
        display.device_separator,
        display.sample_rate_choice,
        display.sample_rate_separator,
        display.buffer_size_choice,
    ]
    assert all(part is not None and part in display.children for part in audio_parts)
    assert len({id(part) for part in audio_parts}) == len(audio_parts)
    assert [display.children.index(part) for part in audio_parts] == sorted(
        display.children.index(part) for part in audio_parts
    )
    display.step()  # Rack's choices read the port every frame
    midi_display = app.MidiDisplay()
    midi_display.box.size = Vec(130, 90)
    out = midi.Output()
    midi_display.set_midi_port(out)
    midi_parts = [
        midi_display.driver_choice,
        midi_display.driver_separator,
        midi_display.device_choice,
        midi_display.device_separator,
        midi_display.channel_choice,
    ]
    assert all(part is not None and part in midi_display.children for part in midi_parts)
    assert len({id(part) for part in midi_parts}) == len(midi_parts)
    assert [midi_display.children.index(part) for part in midi_parts] == sorted(
        midi_display.children.index(part) for part in midi_parts
    )
    midi_display.step()
    display.close()
    midi_display.close()


def test_buttons_and_menus() -> None:
    port = audio.Port()
    button = app.AudioButton()
    button.set_audio_port(port)
    assert button.port is port
    midi_button = app.MidiButton()
    queue = midi.InputQueue()
    midi_button.set_midi_port(queue)
    assert midi_button.port is queue
    for cls in (
        componentlibrary.AudioButton_ADAT,
        componentlibrary.AudioButton_USB_B,
        componentlibrary.MidiButton_MIDI_DIN,
    ):
        b = cls()  # Rack's classes, with Rack's artwork
        assert b.box.size.x > 0
        b.close()
    menu = ui.Menu()
    app.append_audio_menu(menu, port)
    audio_items = len(menu.children)
    app.append_midi_menu(menu, queue)
    assert audio_items > 0 and len(menu.children) > audio_items
    menu.close()
    button.close()
    midi_button.close()


def test_the_racks_containers_hold_their_own_kind() -> None:
    with pytest.raises(OwnershipError, match="only cable widgets"):
        rack().get_cable_container().add_child(w.Widget())
    with pytest.raises(OwnershipError, match="only module widgets"):
        rack().get_module_container().add_child(w.Widget())
    with pytest.raises(OwnershipError, match="plug container"):
        rack().get_plug_container().add_child(w.Widget())
    cable = app.CableWidget()
    holder = w.Widget()
    with pytest.raises(OwnershipError, match="lives only in Rack's rack"):
        holder.add_child(cable)
    with pytest.raises(OwnershipError, match="Add event"):
        cable.on_add(w.AddEvent())  # Rack's dispatch alone sends it
    with pytest.raises(OwnershipError, match="engine"):
        cable.update_cable()  # outside the rack
    holder.close()
    cable.close()
