# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-34: the configuration, state-and-events and menus examples' modules inside Rack's live UI.

The plugin's entry setup imported the three examples, which added their models. The
configuration modules are compared with native/tests/controls_reference.hpp, the same
modules written in C++ and run here; the others are driven through Rack's engine,
its events and their widgets' context menus.
"""

import gc
import json
import runpy
import time
import warnings
from collections.abc import Callable
from pathlib import Path
from typing import Any

import _rackpy_host
import pytest
import rackpystateevents_core as core
from _rackpy_host import _testing as HOOKS
from rackpymenus.switchboard import NOTES, Switchboard
from rackpystateevents.sequencer import SETTINGS, Sequencer

from rackpy import raw
from rackpy.raw import app, ui
from rackpy.raw import widget as w
from rackpy.raw.engine import Module, Port, SwitchQuantity, SwitchQuantityView


def until(condition: Callable[[], bool], seconds: float = 2.0) -> bool:
    """Rack's engine runs on its own thread; give it time to process frames."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.005)
    return condition()


def patch(port: Port, channels: int) -> None:
    """Marks a port as Rack does when a cable connects: set_channels cannot, as an unpatched port keeps 0 channels."""
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)  # writing Port.channels directly is deprecated in Rack
        port.channels = channels


def make(slug: str) -> Module:
    model = raw.plugin.get_model("RackPy", slug)
    assert model is not None, slug
    module = model.create_module()
    assert isinstance(module, Module)
    return module


@pytest.fixture(scope="module")
def reference() -> dict[str, Any]:
    data: dict[str, Any] = json.loads(HOOKS.controls_reference())
    return data


@pytest.mark.parametrize("slug", ["Shaper", "Splitter"])
def test_controls_ports_lights_and_routes_match_cpp(reference: dict[str, Any], slug: str) -> None:
    expected = reference[slug.lower()]
    m = make(slug)
    try:
        assert len(expected["controls"]) == len(m.params)
        for i, want in enumerate(expected["controls"]):
            q = m.get_param_quantity(i)
            got = {
                "name": q.name,
                "label": q.get_label(),
                "unit": q.get_unit(),
                "description": q.get_description(),
                "min": q.get_min_value(),
                "max": q.get_max_value(),
                "default": q.get_default_value(),
                "snap": q.snap_enabled,
                "smooth": q.smooth_enabled,
                "switch": isinstance(q, SwitchQuantity | SwitchQuantityView),
                "labels": list(q.labels) if isinstance(q, SwitchQuantity | SwitchQuantityView) else [],
            }
            assert got == {k: v for k, v in want.items() if k != "cases"}, want["name"]
            for requested, value, display, text in want["cases"]:
                q.set_value(requested)
                assert (q.get_value(), q.get_display_value_string(), q.get_string()) == (value, display, text)
            q.reset()
        infos = (
            [m.get_input_info(i) for i in range(len(m.inputs))],
            [m.get_output_info(i) for i in range(len(m.outputs))],
        )
        for kind, ports in zip(("inputs", "outputs"), infos, strict=True):
            assert [[p.get_name(), p.get_full_name(), p.get_description()] for p in ports] == expected[kind]
        lights = [m.get_light_info(i) for i in range(len(m.lights))]
        assert [[li.get_name(), li.get_description()] for li in lights if li is not None] == expected["lights"]
        assert [[r.input_id, r.output_id] for r in m.bypass_routes] == expected["routes"]
    finally:
        m.close()


def test_the_shaper_tooltips_the_readme_lists() -> None:
    m = make("Shaper")
    try:
        assert [m.get_param_quantity(i).get_string() for i in range(len(m.params))] == [
            "Level: 0 dB",
            "Scale: 1",
            "Offset: 0 V",
            "Tremolo depth: 0%",
            "Tremolo rate: 1 Hz",
            "Shape: Bipolar",
            "Mute while held",
        ]
    finally:
        m.close()


def replay(slug: str, scenario: dict[str, Any], rate: float) -> float:
    """Steps a module of the example's model through the C++ scenario; returns the largest difference."""
    m = make(slug)
    try:
        for i, value in enumerate(scenario["params"]):
            m.params[i].value = value
        channels = scenario["channels"]
        patch(m.inputs[0], channels)
        for output in m.outputs:
            patch(output, 1)
        press, start, stop = scenario["press"]
        step = HOOKS.module_process_bypass if scenario["bypassed"] else HOOKS.module_process
        worst = 0.0
        for k, frame in enumerate(scenario["frames"]):
            for c, volts in enumerate(frame[0]):
                m.inputs[0].set_voltage(volts, c)
            if press >= 0:
                m.params[press].value = 1.0 if start <= k < stop else 0.0
            step(m, rate, k)
            outputs = frame[1 : 1 + len(m.outputs)]
            for o, want in enumerate(outputs):
                got = [m.outputs[o].get_voltage(c) for c in range(m.outputs[o].get_channels())]
                assert len(got) == len(want), (scenario["name"], k, o)
                worst = max([worst, *(abs(a - b) for a, b in zip(got, want, strict=True))])
            for light, want_brightness in enumerate(frame[1 + len(m.outputs) :]):
                worst = max(worst, abs(m.lights[light].get_brightness() - want_brightness))
        return worst
    finally:
        m.close()


@pytest.mark.parametrize("slug", ["Shaper", "Splitter"])
def test_frames_match_cpp_processed_and_bypassed(reference: dict[str, Any], slug: str) -> None:
    for scenario in reference[slug.lower()]["scenarios"]:
        worst = replay(slug, scenario, reference["rate"])
        print(f"{slug} {scenario['name']}: largest difference from C++ {worst:.3g} V")
        assert worst <= 1e-5, scenario["name"]


def test_the_factory_presets_are_where_rack_looks_and_load() -> None:
    model = raw.plugin.get_model("RackPy", "Shaper")
    assert model is not None
    directory = Path(model.get_factory_preset_directory())
    names = sorted(p.name for p in directory.iterdir())  # Rack sorts them so, and hides the "01_" prefixes
    assert names == ["01_Gentle tremolo.vcvm", "02_Full-wave rectifier.vcvm", "03_Positive half at +6 dB.vcvm"]
    m = make("Shaper")
    HOOKS.engine_adopt(m)
    widget = model.create_module_widget(m)
    assert isinstance(widget, app.ModuleWidget)
    try:
        for name in names:
            preset = json.loads((directory / name).read_text())
            widget.load(str(directory / name))  # what choosing it in the module's Preset menu does, without undo
            want = [p["value"] for p in preset["params"]]
            assert [m.params[p["id"]].value for p in preset["params"]] == pytest.approx(want, rel=1e-7), name  # float32
    finally:
        widget.close()


def test_the_sequencer_steps_saves_and_handles_events() -> None:
    m = make("Sequencer")
    sequencer = Sequencer.of(m)
    assert sequencer is not None
    length = m.get_param_quantity(0)
    assert (length.snap_enabled, length.reset_enabled, length.randomize_enabled) == (True, False, False)
    assert m.params[0].value == SETTINGS.default_length == core.DEFAULT_LENGTH
    # JSON state: what it understands, nothing else.
    assert m.data_to_json() == {"version": 1, "range": 0}
    m.data_from_json({"version": 7, "range": 3, "from the future": [1, 2]})
    m.data_from_json({"range": "2"})
    assert sequencer.state.range == 3 and m.data_to_json() == {"version": 1, "range": 3}
    # Stepped on the main thread: one step per clock rise, wrapping at the length, at 10 V full scale.
    m.params[0].value = 3
    patch(m.inputs[0], 1)
    seen = []
    for k in range(80):
        m.inputs[0].set_voltage(10.0 if k % 10 < 5 else 0.0)
        HOOKS.module_process(m, 48000, k)
        if k % 10 == 0:
            seen.append(m.outputs[0].get_voltage())
    # Rack's SchmittTrigger starts high, so a clock already high at frame 0 is not a rise.
    assert seen == pytest.approx([10 * core.default_pattern()[i % 3] for i in range(8)])
    m.close()

    m = make("Sequencer")
    sequencer = Sequencer.of(m)
    assert sequencer is not None
    HOOKS.engine_adopt(m)  # on_add: no patch storage yet, so the default pattern stays
    try:
        assert sequencer.state.pattern == core.default_pattern()
        sequencer.state.pattern = [0.5] * core.STEPS
        sequencer.set_range(1)
        assert until(lambda: m.outputs[0].get_voltage() == pytest.approx(1.0))
        HOOKS.engine_prepare_save()  # Rack's save: on_save writes the pattern file
        saved = m.patch_storage(create=False) / core.PATTERN_FILE
        assert saved.read_bytes() == core.State(pattern=[0.5] * core.STEPS).pattern_bytes()
        SETTINGS.default_length = 12
        m.params[0].value = 5
        HOOKS.engine_randomize(m)  # Rack randomizes, then on_randomize: a new pattern, a length from LENGTHS
        assert sequencer.state.pattern != [0.5] * core.STEPS and all(0 <= v <= 1 for v in sequencer.state.pattern)
        assert m.params[0].value in core.LENGTHS and sequencer.state.range == 1
        first = sequencer.state.pattern[0]
        assert until(lambda: m.outputs[0].get_voltage() == pytest.approx(2 * first, abs=1e-6))
        HOOKS.engine_reset(m)  # Initialize: the default state, the plugin's default length, the first step
        assert sequencer.state == core.State() and m.params[0].value == 12
        assert until(lambda: m.outputs[0].get_voltage() == pytest.approx(0.0))
    finally:
        SETTINGS.default_length = core.DEFAULT_LENGTH
        HOOKS.engine_delete(m, None)


def menu_of(widget: app.ModuleWidget) -> tuple[ui.Menu, dict[str, ui.MenuItem], list[str]]:
    """A menu at the mouse, in a MenuOverlay as Rack opens one (a submenu needs it), with the widget's items."""
    menu = raw.create_menu()
    widget.append_context_menu(menu)
    items = {c.text: c for c in menu.children if isinstance(c, ui.MenuItem)}
    labels = [c.text for c in menu.children if isinstance(c, ui.MenuLabel)]
    return menu, items, labels


def dismiss(menu: ui.Menu) -> None:
    overlay = menu.parent
    assert isinstance(overlay, ui.MenuOverlay)
    overlay.request_delete()


def choose(menu: ui.Menu, item: ui.MenuItem, text: str) -> None:
    """Opens a submenu as the pointer does, and chooses one of its items."""
    item.on_enter(w.EnterEvent())
    child = menu.child_menu
    assert child is not None
    [entry] = [c for c in child.children if isinstance(c, ui.MenuItem) and c.text == text]
    entry.do_action(False)


def test_every_switchboard_menu_item_works() -> None:
    model = raw.plugin.get_model("RackPy", "Switchboard")
    assert model is not None
    preview = model.create_module_widget(None)  # the browser's: no module, so no items
    assert isinstance(preview, app.ModuleWidget)
    menu, items, labels = menu_of(preview)
    assert not menu.children
    dismiss(menu)
    preview.close()

    m = make("Switchboard")
    HOOKS.engine_adopt(m)
    widget = model.create_module_widget(m)
    assert isinstance(widget, app.ModuleWidget)
    board = Switchboard.of(m)
    assert board is not None
    pitch = m.outputs[0]

    def volts(expected: float) -> bool:
        return until(lambda: pitch.get_voltage() == pytest.approx(expected, abs=1e-6))

    try:
        menu, items, labels = menu_of(widget)
        assert labels == ["Pitch"] and sum(isinstance(c, ui.MenuSeparator) for c in menu.children) == 2
        assert list(items) == [
            "Up a semitone",
            "Down a semitone",
            "Middle C",
            "Presets",
            "Octave",
            "Note",
            "Muted",
            "Inverted",
        ]
        menu.step()  # Rack's step: check marks and the index submenus' labels
        assert items["Middle C"].right_text.endswith(ui.CHECKMARK_STRING)
        assert items["Note"].right_text.startswith("C ") and items["Octave"].right_text.startswith("0 ")
        items["Up a semitone"].do_action(False)
        assert (board.octave, board.note) == (2, 1) and volts(1 / 12)
        menu.step()
        assert not items["Middle C"].right_text.endswith(ui.CHECKMARK_STRING)
        items["Middle C"].do_action(False)
        assert (board.octave, board.note) == (2, 0) and volts(0.0)
        choose(menu, items["Presets"], "A 440")
        assert (board.octave, board.note) == (2, 9) and volts(0.75)
        choose(menu, items["Octave"], "+2")
        assert (board.octave, board.note) == (4, 9) and volts(2.75)
        choose(menu, items["Note"], "G")  # the "pointer" item writes Choice.value
        assert board.note == NOTES.index("G") and volts(2 + 7 / 12)
        items["Inverted"].do_action(False)
        assert board.inverted and volts(-(2 + 7 / 12))
        items["Muted"].do_action(False)
        assert board.muted and volts(0.0) and until(lambda: m.lights[0].get_brightness() == 1.0)
        assert m.data_to_json() == {"octave": 4, "note": 7, "muted": True, "inverted": True}
        dismiss(menu)
        board.choose(4, 11)  # the top: "Up a semitone" is disabled
        menu, items, _ = menu_of(widget)
        assert items["Up a semitone"].disabled and not items["Down a semitone"].disabled
        dismiss(menu)
        board.choose(0, 0)  # the bottom
        menu, items, _ = menu_of(widget)
        assert items["Down a semitone"].disabled and not items["Up a semitone"].disabled
        dismiss(menu)
        # Saved state from a patch: what it understands, nothing else.
        m.data_from_json({"octave": 1, "note": 99, "muted": "no", "inverted": False, "later": 1})
        assert (board.octave, board.note, board.muted, board.inverted) == (1, 0, True, False)  # note 99 ignored
        HOOKS.engine_reset(m)  # Initialize: middle C, neither muted nor inverted
        assert (board.octave, board.note, board.muted, board.inverted) == (2, 0, False, False) and volts(0.0)

        # Rack's own module menu: in Rack's rack, Rack calls append_context_menu after its items.
        context = raw.context_get()
        assert context is not None and context.scene is not None
        context.scene.rack.add_module_at_mouse(widget)
        widget.create_context_menu()
        rack_labels = HOOKS.take_scene_menu_labels()  # and dismisses the menu
        assert rack_labels[0] == "Switchboard" and rack_labels[-1] == "Pitch", rack_labels
        context.scene.rack.remove_module(widget)

        # A menu outliving its module: each item does nothing, and nothing raises.
        menu, items, _ = menu_of(widget)
    finally:
        widget.close()  # deletes its module, as Rack's does
    gc.collect()
    assert not m.alive
    raised = _rackpy_host.dispatch_stats()["raised"]
    for name in ("Up a semitone", "Middle C", "Muted", "Inverted"):
        items[name].do_action(False)
    choose(menu, items["Presets"], "Low C")
    choose(menu, items["Note"], "D")
    assert _rackpy_host.dispatch_stats()["raised"] == raised
    dismiss(menu)


def test_the_laboratory_events_script(examples: Path) -> None:
    script = runpy.run_path(str(examples / "state-and-events" / "laboratory" / "events_on_a_bench.py"))
    result = script["main"]()
    print(f"laboratory script: {result}")
    assert result["events"] == ["rate 48000", "reset", "rate 96000", "randomize"]
    # The same 10 ms time constant at both rates: 1 - 1/e after 10 ms.
    assert result["at_48k"] == pytest.approx(0.632, abs=2e-3) and result["at_96k"] == pytest.approx(0.632, abs=2e-3)
