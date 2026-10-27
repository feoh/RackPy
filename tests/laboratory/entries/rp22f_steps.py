# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22F test entry module, in windowed Rack with the test hooks: what needs Rack's UI steps.

Rack's own main-thread delivery (its computer keyboard driver's route) and Rack's own menu items release what
they kept alive at the next UI step; laboratory drivers and devices dropped on a Python thread are destroyed at
the next UI step, on the main thread.
"""

import gc
import json
import threading
import traceback
import weakref
from collections.abc import Generator
from pathlib import Path
from typing import Any

from _rackpy_host import _testing

from rackpy import laboratory
from rackpy.errors import LaboratoryError
from rackpy.raw import app, audio, midi, ui
from rackpy.raw import widget as w

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "rp22f_config.json").read_text())
RESULT: dict[str, Any] = {}
LOOPBACK = -12
laboratory.enable()


def note(n: int) -> midi.Message:
    message = midi.Message()
    message.bytes = [0x90, n, 1]
    return message


def keyboard_route() -> Generator[None]:
    """As Rack's keyboard driver delivers: no Python call into Rack's code ends after Rack's loop."""
    doomed = laboratory.MidiDriver("doomed")
    doomed.add_input_device(laboratory.MidiInputDevice("doomed device"))
    bystander = midi.Input()
    doomed.connect(bystander)
    held: dict[str, Any] = {"driver": doomed}
    refs: dict[str, weakref.ref[Any]] = {"driver": weakref.ref(doomed)}
    del doomed
    inside: dict[str, Any] = {}

    class Dropping(laboratory.MidiInput):
        def on_message(self, message: midi.Message) -> None:
            held.clear()
            gc.collect()
            inside["alive"] = [refs["port"]() is not None, refs["driver"]() is not None]
            try:
                self.set_device_id(-1)
                inside["refused"] = False
            except LaboratoryError:
                inside["refused"] = True

    dropping, other = Dropping(), midi.Input()
    for port in (dropping, other):
        port.set_driver_id(LOOPBACK)
        port.set_device_id(13)
    refs["port"] = weakref.ref(dropping)
    held["port"] = dropping
    del dropping
    _testing.deliver_as_racks_keyboard(other, note(60))  # Rack's loop reaches Dropping through `other`'s device
    gc.collect()
    RESULT["keyboard_inside"] = inside
    RESULT["keyboard_after_call"] = [refs["port"]() is not None, refs["driver"]() is not None]
    yield
    yield
    gc.collect()
    RESULT["keyboard_after_step"] = [refs["port"]() is not None, refs["driver"]() is not None]
    RESULT["keyboard_bystander"] = list(_testing.port_targets(bystander))
    other.set_device_id(-1)


def menu_route() -> Generator[None]:
    """Rack's own device menu item calls setDeviceId: a driver an override drops goes at the next UI step."""
    holder: dict[str, Any] = {}

    class Dropping(laboratory.MidiDriver):
        def unsubscribe_input(self, device_id: int, input: midi.Input) -> None:
            super().unsubscribe_input(device_id, input)
            holder.clear()

    driver = Dropping()
    driver.add_input_device(laboratory.MidiInputDevice("x"))
    driver.add_input_device(laboratory.MidiInputDevice("y"))
    port = midi.Input()
    driver.connect(port)
    gone = weakref.ref(driver)
    holder["driver"] = driver
    del driver
    menu = ui.Menu()
    app.append_midi_menu(menu, port)
    children = list(menu.children)
    fired = []
    for index in range(len(children) - 1, -1, -1):  # Rack's device items come last, device 1 the last of them
        children[index].on_action(w.ActionEvent())  # Rack's item's onAction: MidiDeviceValueItem calls setDeviceId
        fired.append(index)
        if port.get_device_id() != 0:
            break
    RESULT["menu_items"] = [len(children), fired]
    del children
    menu.close()
    gc.collect()
    RESULT["menu_after_call"] = [gone() is not None, port.get_device_id()]
    yield
    yield
    gc.collect()
    RESULT["menu_after_step"] = [gone() is not None, list(_testing.port_targets(port))]


def off_main_thread() -> Generator[None]:
    """Laboratory drivers and devices whose last reference goes on a Python thread go on the main thread."""
    midi_driver = laboratory.MidiDriver("off")
    midi_driver.add_input_device(laboratory.MidiInputDevice("d"))
    midi_port = midi.Input()
    midi_driver.connect(midi_port)
    audio_driver = laboratory.AudioDriver("off")
    audio_driver.add_device(laboratory.AudioDevice("a"))
    audio_port = audio.Port()
    audio_driver.connect(audio_port)
    output_device = laboratory.MidiOutputDevice()
    output = midi.Output()
    output_device.connect(output)
    gone = [weakref.ref(o) for o in (midi_driver, audio_driver, output_device)]
    box: list[Any] = [midi_driver, audio_driver, output_device]
    del midi_driver, audio_driver, output_device

    def drop() -> None:
        box.clear()  # the last references: their Python objects go on this thread
        RESULT["off_thread_main"] = threading.current_thread() is threading.main_thread()

    thread = threading.Thread(target=drop)
    thread.start()
    thread.join()

    def targets() -> list[Any]:
        return [list(_testing.port_targets(p)) for p in (midi_port, audio_port, output)]

    RESULT["off_python_objects"] = [r() is None for r in gone]
    RESULT["off_before_step"] = targets()
    RESULT["off_native_answers"] = [midi_port.get_device_ids(), audio_port.get_device_ids()]  # still alive natively
    yield
    yield
    RESULT["off_after_step"] = targets()
    RESULT["off_after_step_ids"] = [midi_port.get_device_ids(), audio_port.get_device_ids()]


def scenario() -> Generator[None]:
    yield from keyboard_route()
    yield from menu_route()
    yield from off_main_thread()


running: list[Generator[None]] = []
done = [False]


def finish() -> None:
    Path(CONFIG["result"]).write_text(json.dumps(RESULT))
    _testing.close_window()


def on_step(frame: int, handle: object) -> None:
    if done[0] or frame < 3:
        return
    try:
        if not running:
            running.append(scenario())
        next(running[0])
    except StopIteration:
        done[0] = True
        finish()
    except BaseException:
        RESULT["error"] = traceback.format_exc()
        done[0] = True
        finish()


_testing.set_step_callback(on_step)
