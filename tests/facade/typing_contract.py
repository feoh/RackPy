# SPDX-License-Identifier: GPL-3.0-or-later
"""Static facade contracts: mypy checks these without starting a Rack host."""

from typing import TYPE_CHECKING, assert_type

if TYPE_CHECKING:
    from rackpy.audio import open_port
    from rackpy.drawing import Painter
    from rackpy.dsp.graph import Graph, Plan, Signal
    from rackpy.midi import open_input
    from rackpy.plugin import ModuleClass
    from rackpy.raw.audio import Port
    from rackpy.raw.engine import Module
    from rackpy.raw.midi import Input, InputQueue
    from rackpy.raw.widget import GraphicsContext, Widget
    from rackpy.services.network import delete_json, get_json, post_json, put_json
    from rackpy.types import JSON

    def drawing(args: Widget.DrawArgs, vg: GraphicsContext) -> None:
        assert_type(Painter(args), Painter)
        assert_type(Painter(vg), Painter)
        Painter(object())  # type: ignore[arg-type]  # deliberately invalid

    class CustomAudio(Port): ...

    class CustomMidi(Input): ...

    graph = Graph()
    signal = graph.input(0)
    assert_type(signal * 0.5 + graph.param(0), Signal)
    graph.output(0, signal)
    assert_type(graph.plan(), Plan)
    assert_type(open_port(1), Port)
    assert_type(open_port(1, kind=CustomAudio), CustomAudio)
    assert_type(open_input(1), InputQueue)
    assert_type(open_input(1, kind=CustomMidi), CustomMidi)
    body: JSON = {"value": 1, "items": [True, None]}
    assert_type(get_json("https://example.invalid"), JSON)
    assert_type(post_json("https://example.invalid", body), JSON)
    strings: list[str] = ["item"]
    assert_type(post_json("https://example.invalid", strings), JSON)
    assert_type(put_json("https://example.invalid", body), JSON)
    assert_type(delete_json("https://example.invalid"), JSON)

    def rejects_wrong_event(module: ModuleClass) -> None:
        module.on_port_change("not an event")  # type: ignore[arg-type]

    class TypedCallbacks(ModuleClass):
        def from_json(self, data: JSON) -> None:
            assert_type(data, JSON)

        def to_json(self) -> object:
            return {"words": ["a", "b"]}

        def on_add(self, event: Module.AddEvent) -> None: ...
        def on_remove(self, event: Module.RemoveEvent) -> None: ...
        def on_bypass(self, event: Module.BypassEvent) -> None: ...
        def on_un_bypass(self, event: Module.UnBypassEvent) -> None: ...
        def on_port_change(self, event: Module.PortChangeEvent) -> None: ...
        def on_reset(self, event: Module.ResetEvent) -> None: ...
        def on_randomize(self, event: Module.RandomizeEvent) -> None: ...
        def on_save(self, event: Module.SaveEvent) -> None: ...
