# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38 stress models. The stress suite's entry setup imports this while Rack loads the plugin.

``Churn`` is a module with everything a real one has that lives across Rack's
threads: a compiled graph kernel (a sine, a frame counter, and lanes to both
expander neighbours), a bypass kernel, JSON state, a file in patch storage, every
lifecycle event, a panel with ports, a context menu, and a child widget that steps
and draws. Every Python callback of it also obeys :class:`Hostility`, so that a
test can make them all slow, or all raise, at once.
"""

import json
import time
import weakref
from typing import Any, ClassVar, Self

from rackpy import raw
from rackpy.dsp.graph import Graph
from rackpy.engine import Module
from rackpy.engine.expanders import graph_messages
from rackpy.plugin import ModuleClass, plugin
from rackpy.raw import app, ui
from rackpy.raw import componentlibrary as cl
from rackpy.raw import widget as w
from rackpy.raw.math import Vec
from rackpy.types import JSON

STEPS = 16
FRAMES = 1  # output carrying the kernel's frame counter
STORAGE = "state.json"


class Hostility:
    """What every callback below does besides its work: sleep ``slow`` seconds, then raise if ``fail``."""

    slow = 0.0
    fail = False
    calls = 0


def hostile() -> None:
    Hostility.calls += 1
    if Hostility.slow:
        time.sleep(Hostility.slow)
    if Hostility.fail:
        raise RuntimeError("RP-38 hostile callback")


def churn_graph() -> Graph:
    g = Graph()
    frames = g.delay(0.0)
    frames.feed(frames + 1)
    g.output(0, g.sin2pi(g.phasor(g.pitch_to_freq(g.input(0) + g.param(0)))) * g.control("amp", 5.0))
    g.output(FRAMES, frames)
    g.output(2, g.expander_receive(0) + g.expander_receive(1))
    g.expander_send(0, frames)
    g.expander_send(1, frames)
    g.light(0, g.input(1))
    return g


def bypass_graph() -> Graph:
    g = Graph()
    g.output(0, g.input(0) * 0.5)
    return g


GRAPH, BYPASS = churn_graph(), bypass_graph()


def valid_state(data: object) -> dict[str, Any] | None:
    if not isinstance(data, dict):
        return None
    steps, label = data.get("steps"), data.get("label")
    if not isinstance(steps, list) or len(steps) != STEPS or not isinstance(label, str):
        return None
    if not all(isinstance(v, int | float) and not isinstance(v, bool) for v in steps):
        return None
    return {"steps": [float(v) for v in steps], "label": label}


class Churn(ModuleClass):
    _instances: ClassVar[weakref.WeakValueDictionary[int, Churn]] = weakref.WeakValueDictionary()

    @classmethod
    def of(cls, module: Module | None) -> Self:
        found = cls._instances.get(id(module)) if module is not None else None
        assert isinstance(found, cls) and found.module is module, "not a Churn module"
        return found

    def configure(self) -> None:
        m = self.module
        m.config(1, 2, 3, 1)
        m.config_param(0, -2.0, 2.0, 0.0, "Pitch", " V")
        graph_messages(m.left_expander)
        graph_messages(m.right_expander)
        self.state: dict[str, Any] = {"steps": [0.0] * STEPS, "label": ""}
        self.kernel = GRAPH.compile()
        m.set_kernel(self.kernel)
        m.set_bypass_kernel(BYPASS.compile())
        self.events: list[str] = []
        type(self)._instances[id(m)] = self

    def replace_state(self, state: dict[str, Any]) -> None:
        self.state = state
        self.kernel.set("amp", state["steps"][0])

    def to_json(self) -> object:
        hostile()
        return self.state

    def from_json(self, data: JSON) -> None:
        hostile()
        state = valid_state(data)
        if state is not None:
            self.replace_state(state)

    def event(self, name: str) -> None:
        if len(self.events) < 64:
            self.events.append(name)
        hostile()

    def on_add(self, event: Module.AddEvent) -> None:
        path = self.module.patch_storage(create=False) / STORAGE
        if path.is_file():
            state = valid_state(json.loads(path.read_text()))
            if state is not None:
                self.replace_state(state)
        self.event("add")

    def on_remove(self, event: Module.RemoveEvent) -> None:
        self.event("remove")

    def on_bypass(self, event: Module.BypassEvent) -> None:
        self.event("bypass")

    def on_un_bypass(self, event: Module.UnBypassEvent) -> None:
        self.event("un_bypass")

    def on_port_change(self, event: Module.PortChangeEvent) -> None:
        self.event("port_change")

    def on_reset(self, event: Module.ResetEvent) -> None:
        self.replace_state({"steps": [0.0] * STEPS, "label": ""})
        self.event("reset")

    def on_randomize(self, event: Module.RandomizeEvent) -> None:
        self.replace_state({"steps": [float(i % 5) for i in range(STEPS)], "label": "random"})
        self.event("randomize")

    def on_save(self, event: Module.SaveEvent) -> None:
        (self.module.patch_storage() / STORAGE).write_text(json.dumps(self.state))
        self.event("save")


class Restless(w.Widget):
    """A child widget whose every step and draw is a Python callback."""

    def step(self) -> None:
        hostile()
        super().step()

    def draw(self, args: w.DrawArgs) -> None:
        hostile()


class ChurnWidget(app.ModuleWidget):
    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.box.size = Vec(app.RACK_GRID_WIDTH * 4, app.RACK_GRID_HEIGHT)
        for i in range(2):
            self.add_input(raw.create_input_centered(cl.PJ301MPort, Vec(30, 60 + 40 * i), module, i))
        for i in range(3):
            self.add_output(raw.create_output_centered(cl.PJ301MPort, Vec(30, 180 + 40 * i), module, i))
        restless = Restless()
        restless.box.size = Vec(20, 20)
        self.add_child(restless)

    def append_context_menu(self, menu: ui.Menu | w.HostWidget) -> None:
        hostile()
        if isinstance(menu, ui.Menu):
            menu.add_child(raw.create_menu_label("Churn"))


plugin.add_model("Churn", Churn, widget=ChurnWidget)
