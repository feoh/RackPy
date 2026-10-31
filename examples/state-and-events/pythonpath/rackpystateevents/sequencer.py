"""Sequencer: a module's events and its saved state.

Generated from res/Sequencer.svg by bin/rackpy-plugin add-module, then rewritten as a
ModuleClass, whose instance lives as long as Rack's module and holds its Python state:

- the range, saved in the patch as the module's JSON (to_json/from_json);
- the sixteen step values, saved as a file in the module's patch storage (on_add reads
  it, on_save writes it);
- Initialize (on_reset) and Randomize (on_randomize) change that state, and set the
  LENGTH parameter explicitly, because Rack's own reset and randomize leave it alone;
- the plugin-wide default length is in Rack's settings.json (see SETTINGS).

The audio is rackpystateevents_core's graph, compiled into a native kernel. Python
hands the kernel its step values and range as controls; it never runs per sample.
Rack's sample-rate event reaches the kernel natively.
"""

import random
import weakref
from enum import IntEnum
from typing import ClassVar, Self

import rackpystateevents_core as core

from rackpy import assets, raw
from rackpy.engine import Module
from rackpy.plugin import ModuleClass
from rackpy.raw import app, ui
from rackpy.raw import componentlibrary as cl
from rackpy.raw.math import Vec
from rackpy.raw.widget import HostWidget
from rackpy.raw.window import mm2px
from rackpy.types import JSON


class Param(IntEnum):
    LENGTH = core.LENGTH


class Input(IntEnum):
    CLOCK = core.CLOCK
    RESET = core.RESET


class Output(IntEnum):
    CV = core.CV


class Light(IntEnum):
    STEP = core.STEP_LIGHT


GRAPH = core.sequencer_graph()
SETTINGS = core.Settings()
"""The plugin's settings; the entry package hands them to Rack (plugin.set_settings_handlers)."""


class Sequencer(ModuleClass):
    """One per sequencer in the patch."""

    _instances: ClassVar[weakref.WeakValueDictionary[int, Sequencer]] = weakref.WeakValueDictionary()

    @classmethod
    def of(cls, module: Module | None) -> Self | None:
        """The instance for a module (its widget has only the module), or None."""
        found = cls._instances.get(id(module)) if module is not None else None
        return found if isinstance(found, cls) and found.module is module else None

    def configure(self) -> None:
        m = self.module
        m.config(len(Param), len(Input), len(Output), len(Light))
        length = m.config_param(Param.LENGTH, 1.0, core.STEPS, core.DEFAULT_LENGTH, "Length", " steps")
        length.snap_enabled = True
        # Rack's Initialize and Randomize skip it: on_reset and on_randomize set it themselves.
        length.reset_enabled = False
        length.randomize_enabled = False
        m.params[Param.LENGTH].value = SETTINGS.default_length
        m.config_input(Input.CLOCK, "Clock")
        m.config_input(Input.RESET, "Reset")
        m.config_output(Output.CV, "Step voltage")
        m.config_light(Light.STEP, "Step")
        self.state = core.State()
        self.kernel = GRAPH.compile()
        m.set_kernel(self.kernel)
        self._send()
        type(self)._instances[id(m)] = self

    def _send(self) -> None:
        """Hand the state to the kernel: its controls reach the engine at the next frame."""
        for i, value in enumerate(self.state.pattern):
            self.kernel.set(core.step_name(i), value)
        self.kernel.set("range", core.RANGES[self.state.range])

    def set_range(self, index: int) -> None:
        self.state.range = index
        self._send()

    # Saved in the patch, with the parameters.

    def to_json(self) -> object:
        return self.state.to_json()

    def from_json(self, data: JSON) -> None:
        self.state.load_json(data)
        self._send()

    # Saved next to the patch. Rack gives the module a directory once its engine has the module.

    def on_add(self, event: Module.AddEvent) -> None:
        path = self.module.patch_storage(create=False) / core.PATTERN_FILE
        if path.is_file() and self.state.load_pattern(path.read_bytes()):
            self._send()

    def on_save(self, event: Module.SaveEvent) -> None:
        (self.module.patch_storage() / core.PATTERN_FILE).write_bytes(self.state.pattern_bytes())

    # Initialize and Randomize. Rack has already reset or randomized the other parameters.

    def on_reset(self, event: Module.ResetEvent) -> None:
        self.state = core.State()
        self.module.params[Param.LENGTH].value = SETTINGS.default_length
        self._send()

    def on_randomize(self, event: Module.RandomizeEvent) -> None:
        self.state.pattern = [random.random() for _ in range(core.STEPS)]
        self.module.params[Param.LENGTH].value = random.choice(core.LENGTHS)
        self._send()


def load_settings(data: JSON) -> None:
    SETTINGS.load_json(data)


def save_settings() -> object:
    return SETTINGS.to_json()


class SequencerWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/Sequencer.svg"))))
        grid, right = app.RACK_GRID_WIDTH, self.box.size.x - 2 * app.RACK_GRID_WIDTH
        bottom = app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        for x, y in ((grid, 0), (right, 0), (grid, bottom), (right, bottom)):
            self.add_child(raw.create_widget(cl.ScrewSilver, Vec(x, y)))
        self.add_param(raw.create_param_centered(cl.RoundBlackSnapKnob, mm2px(Vec(15.24, 32.0)), module, Param.LENGTH))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(15.24, 60.0)), module, Input.CLOCK))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(15.24, 80.0)), module, Input.RESET))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(15.24, 108.0)), module, Output.CV))
        self.add_child(
            raw.create_light_centered(cl.SmallLight[cl.YellowLight], mm2px(Vec(15.24, 47.0)), module, Light.STEP)
        )

    def append_context_menu(self, menu: ui.Menu | HostWidget) -> None:
        sequencer = Sequencer.of(self.module)
        if sequencer is None:
            return
        menu.add_child(ui.MenuSeparator())
        menu.add_child(
            raw.create_index_submenu_item(
                "Range", list(core.RANGE_LABELS), lambda: sequencer.state.range, sequencer.set_range
            )
        )

        def set_default(index: int) -> None:
            SETTINGS.default_length = core.LENGTHS[index]

        def default_index() -> int:
            length = SETTINGS.default_length
            return core.LENGTHS.index(length) if length in core.LENGTHS else -1

        labels = [f"{n} steps" for n in core.LENGTHS]
        menu.add_child(raw.create_index_submenu_item("Length after Initialize", labels, default_index, set_default))
