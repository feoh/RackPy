"""Switchboard: a pitch source with no controls on its panel, set from its context menu.

Generated from res/Switchboard.svg by bin/rackpy-plugin add-module, then rewritten as a
ModuleClass. Its widget's append_context_menu adds one of each of Rack's menu helpers:
a label, a separator, actions (one of them disabled at the top of the range), a check
mark, a bool, a bool "pointer", a submenu built when it opens, an index submenu and an
index "pointer" submenu. The choices are the module's Python state, saved in the patch;
each change hands the kernel a new voltage. The kernel only outputs it, natively.

A browser preview has no module, so the widget adds no state-dependent items for it;
an item whose module was deleted while its menu was open does nothing.
"""

import weakref
from enum import IntEnum
from functools import partial
from typing import ClassVar, Self

from rackpy import assets, raw
from rackpy.dsp.graph import Graph
from rackpy.engine import Module
from rackpy.plugin import ModuleClass
from rackpy.raw import app, ui
from rackpy.raw import componentlibrary as cl
from rackpy.raw.math import Vec
from rackpy.raw.widget import HostWidget
from rackpy.raw.window import mm2px
from rackpy.types import JSON


class Output(IntEnum):
    PITCH = 0


class Light(IntEnum):
    MUTED = 0


OCTAVES = ("-2", "-1", "0", "+1", "+2")  # index 2 is middle C's octave
NOTES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
PRESETS = {"Middle C": (2, 0), "A 440": (2, 9), "Low C": (0, 0), "High G": (4, 7)}


def switchboard_graph() -> Graph:
    g = Graph()
    g.output(Output.PITCH, g.control("volts", 0.0))
    g.light(Light.MUTED, g.control("muted", 0.0))
    return g


GRAPH = switchboard_graph()


class Choice:
    """An attribute of the module's state as an object with a ``value``: what the *_ptr_* helpers take.

    A C++ menu item holds a pointer to the field; in Python the helper reads and
    writes ``value``, so setting it here can also hand the kernel the new voltage.
    """

    def __init__(self, owner: Switchboard, name: str) -> None:
        self.owner, self.name = owner, name

    @property
    def value(self) -> object:
        return getattr(self.owner, self.name)

    @value.setter
    def value(self, value: object) -> None:
        setattr(self.owner, self.name, value)
        self.owner.send()


class Switchboard(ModuleClass):
    """One per switchboard in the patch: its octave, note, mute and inversion."""

    _instances: ClassVar[weakref.WeakValueDictionary[int, Switchboard]] = weakref.WeakValueDictionary()

    octave: int
    note: int
    muted: bool
    inverted: bool

    @classmethod
    def of(cls, module: Module | None) -> Self | None:
        """The instance for a module (its widget has only the module), or None."""
        found = cls._instances.get(id(module)) if module is not None else None
        return found if isinstance(found, cls) and found.module is module else None

    def configure(self) -> None:
        m = self.module
        m.config(0, 0, len(Output), len(Light))
        m.config_output(Output.PITCH, "Pitch (1 V/octave)")
        m.config_light(Light.MUTED, "Muted")
        self.octave, self.note, self.muted, self.inverted = 2, 0, False, False
        self.kernel = GRAPH.compile()
        m.set_kernel(self.kernel)
        type(self)._instances[id(m)] = self

    @property
    def volts(self) -> float:
        volts = self.octave - 2 + self.note / 12
        return 0.0 if self.muted else -volts if self.inverted else volts

    def send(self) -> None:
        """Hand the kernel the voltage. A menu can outlive its module: then there is no kernel to tell."""
        if self.module.alive:
            self.kernel.set("volts", self.volts)
            self.kernel.set("muted", 1.0 if self.muted else 0.0)

    def nudge(self, semitones: int) -> None:
        position = min(max(self.octave * 12 + self.note + semitones, 0), len(OCTAVES) * 12 - 1)
        self.octave, self.note = divmod(position, 12)
        self.send()

    def choose(self, octave: int, note: int) -> None:
        self.octave, self.note = octave, note
        self.send()

    def to_json(self) -> object:
        return {"octave": self.octave, "note": self.note, "muted": self.muted, "inverted": self.inverted}

    def from_json(self, data: JSON) -> None:
        if not isinstance(data, dict):
            return
        octave, note = data.get("octave"), data.get("note")
        if isinstance(octave, int) and not isinstance(octave, bool) and 0 <= octave < len(OCTAVES):
            self.octave = octave
        if isinstance(note, int) and not isinstance(note, bool) and 0 <= note < len(NOTES):
            self.note = note
        for name in ("muted", "inverted"):
            if isinstance(flag := data.get(name), bool):
                setattr(self, name, flag)
        self.send()

    def on_reset(self, event: Module.ResetEvent) -> None:
        self.octave, self.note, self.muted, self.inverted = 2, 0, False, False
        self.send()


class SwitchboardWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/Switchboard.svg"))))
        right, bottom = self.box.size.x - 2 * app.RACK_GRID_WIDTH, app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        self.add_child(raw.create_widget(cl.ScrewSilver, Vec(app.RACK_GRID_WIDTH, 0)))
        self.add_child(raw.create_widget(cl.ScrewSilver, Vec(right, bottom)))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(10.16, 108.0)), module, Output.PITCH))
        self.add_child(
            raw.create_light_centered(cl.SmallLight[cl.RedLight], mm2px(Vec(10.16, 90.0)), module, Light.MUTED)
        )

    def append_context_menu(self, menu: ui.Menu | HostWidget) -> None:
        board = Switchboard.of(self.module)
        if board is None:  # no module, as in the browser: nothing to choose
            return
        menu.add_child(ui.MenuSeparator())
        menu.add_child(raw.create_menu_label("Pitch"))
        top = len(OCTAVES) * 12 - 1
        at = board.octave * 12 + board.note
        menu.add_child(raw.create_menu_item("Up a semitone", "", lambda: board.nudge(1), disabled=at >= top))
        menu.add_child(raw.create_menu_item("Down a semitone", "", lambda: board.nudge(-1), disabled=at <= 0))
        menu.add_child(
            raw.create_check_menu_item(
                "Middle C", "", lambda: (board.octave, board.note) == (2, 0), lambda: board.choose(2, 0)
            )
        )

        def fill_presets(submenu: ui.Menu) -> None:  # runs when the submenu opens
            for name, (octave, note) in PRESETS.items():
                submenu.add_child(raw.create_menu_item(name, "", partial(board.choose, octave, note)))

        menu.add_child(raw.create_submenu_item("Presets", "", fill_presets))
        menu.add_child(
            raw.create_index_submenu_item(
                "Octave", list(OCTAVES), lambda: board.octave, lambda octave: board.choose(octave, board.note)
            )
        )
        menu.add_child(raw.create_index_ptr_submenu_item("Note", list(NOTES), Choice(board, "note")))
        menu.add_child(ui.MenuSeparator())

        def set_muted(muted: bool) -> None:
            board.muted = muted
            board.send()

        menu.add_child(raw.create_bool_menu_item("Muted", "", lambda: board.muted, set_muted))
        menu.add_child(raw.create_bool_ptr_menu_item("Inverted", "", Choice(board, "inverted")))
