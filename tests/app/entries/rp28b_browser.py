# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-28B test entry module: a Python model with a Python module widget, through Rack's own UI paths.

Phase 1 drives Rack's real module browser, cables, Delete, undo and redo,
and Model's Python calls; phase 2 reopens the patch Rack saved as it quit.
A script advances one step per frame from the RuntimeProbe's step().
"""

import gc
import importlib
import json
import os
import traceback
import warnings
from collections.abc import Callable, Generator
from pathlib import Path
from typing import Any

from _rackpy_host import _testing

from rackpy import assets, raw
from rackpy.engine import Module
from rackpy.errors import OwnershipError
from rackpy.plugin import plugin
from rackpy.raw import app
from rackpy.raw import widget as w
from rackpy.raw.engine import Port
from rackpy.raw.math import Rect, Vec
from rackpy.raw.window import Svg

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "rp28b_config.json").read_text())
RESULT: dict[str, Any] = {"factory": [], "removes": []}
LEFT, PRESS, RELEASE = 0, 1, 0
VOICES: dict[int, VoiceWidget] = {}
PREVIEWS: list[VoiceWidget] = []
Frames = Generator[None]


def voice(m: Module) -> None:
    m.config(1, 1, 1, 1)
    m.config_param(0, 0, 10, 5, "Pitch")
    m.config_input(0, "Gate")
    m.config_output(0, "Out")
    m.config_light(0, "Active")


class Watch(w.Widget):
    """A plain Python child that records Rack's RemoveEvent."""

    def __init__(self, tag: str) -> None:
        super().__init__()
        self.tag = tag
        self.box = Rect(Vec(), Vec(4, 4))

    def on_remove(self, event: Any) -> None:
        RESULT["removes"].append(self.tag)
        super().on_remove(event)


class Jack(app.SvgPort):
    def __init__(self) -> None:
        super().__init__()
        self.set_svg(Svg.load(str(assets.plugin("res/jack.svg"))))


class Dial(app.Knob):
    def __init__(self) -> None:
        super().__init__()
        self.box.size = Vec(28, 28)


class VoiceWidget(app.ModuleWidget):
    def __init__(self, module: Module | None) -> None:
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always", DeprecationWarning)
            super().__init__(module)  # deprecated constructor, within the factory
        RESULT.setdefault("deprecated_constructor", []).append(
            [module is None, self.module is module, any(issubclass(w.category, DeprecationWarning) for w in caught)]
        )
        RESULT["factory"].append(None if module is None else module.id)
        self.set_panel(raw.create_panel(str(assets.plugin("res/voice.svg"))))
        self.add_param(raw.create_param_centered(Dial, Vec(45, 80), module, 0))
        self.add_output(raw.create_output_centered(Jack, Vec(45, 200), module, 0))
        self.add_input(raw.create_input_centered(Jack, Vec(45, 260), module, 0))
        self.add_child(Watch("preview" if module is None else f"module-{module.id}"))
        if module is None:
            PREVIEWS.append(self)
        else:
            VOICES[module.id] = self


plugin.add_model("PyVoice", voice, widget=VoiceWidget)
module_widget: Any = importlib.import_module("module_widget")  # the documentation's example registers PyBlink


def scene_pos(widget: w.Widget) -> Vec:
    context = raw.context_get()
    assert context is not None and context.event is not None and context.event.root_widget is not None
    return widget.get_relative_offset(widget.box.size.div(2), context.event.root_widget)


def events() -> w.EventState:
    context = raw.context_get()
    assert context is not None and context.event is not None
    return context.event


def wait(frames: int) -> Frames:
    for _ in range(frames):
        yield


def until(condition: Callable[[], bool], limit: int = 90) -> Generator[None, None, bool]:
    for _ in range(limit):
        if condition():
            return True
        yield
    return condition()


def drag(start: Vec, end: Vec) -> None:
    state = events()
    state.handle_hover(start, Vec())
    state.handle_button(start, LEFT, PRESS, 0)
    state.handle_hover(end, end.minus(start))
    state.handle_button(end, LEFT, RELEASE, 0)


def phase1() -> Frames:
    # Rack's module browser: its ModelBoxes ask the factory for previews, with no module.
    _testing.show_browser(True)
    yield from wait(2)
    for char in "PyVoice":
        events().handle_text(Vec(5, 5), ord(char))
    RESULT["preview_seen"] = yield from until(lambda: any(p.alive for p in PREVIEWS))
    preview = next(p for p in PREVIEWS if p.alive)
    RESULT["preview"] = [preview.module is None, isinstance(preview.get_panel(), app.SvgPanel)]
    at = scene_pos(preview)
    events().handle_hover(at, Vec())
    events().handle_button(at, LEFT, PRESS, 0)  # Rack's ModelBox: chooseModel adds the module
    events().handle_button(at, LEFT, RELEASE, 0)
    RESULT["added_from_browser"] = yield from until(lambda: len(VOICES) >= 1)
    first = next(iter(VOICES))
    second = _testing.browser_add_module("RackPy", "PyVoice")
    yield from wait(3)
    a, b = VOICES[first], VOICES[second]
    # Call the ModuleWidget binding directly while Rack owns the widget.
    rw = rack()
    rw.deselect_all()
    before_pos = Vec(a.box.pos.x, a.box.pos.y)  # a.box.pos is a live view
    old_squeeze = raw.settings.settings.squeeze_modules
    raw.settings.settings.squeeze_modules = False
    try:
        start_pos = before_pos.plus(Vec(0, 190))
        rw.on_hover(w.HoverEvent(pos=start_pos))
        a.on_drag_start(w.DragStartEvent(button=LEFT))
        a.on_drag_move(w.DragMoveEvent(button=LEFT))  # records the initial rack position
        rw.on_hover(w.HoverEvent(pos=start_pos.plus(Vec(150, 0))))
        a.on_drag_move(w.DragMoveEvent(button=1))
        right_ignored = a.box.pos == before_pos
        a.on_drag_move(w.DragMoveEvent(button=LEFT))
        RESULT["direct_module_drag"] = [right_ignored, a.box.pos != before_pos]
        a.on_drag_end(w.DragEndEvent(button=LEFT))
        rw.set_module_pos_nearest(a, before_pos)
    finally:
        raw.settings.settings.squeeze_modules = old_squeeze
    out, inp = a.get_output(0), b.get_input(0)
    assert isinstance(out, app.PortWidget) and isinstance(inp, app.PortWidget)
    # Exercise output hover while both ports are initially unconnected.
    start, end = scene_pos(inp), scene_pos(out)
    state = events()
    state.handle_hover(start, Vec())
    state.handle_button(start, LEFT, PRESS, 0)
    state.handle_hover(end, end.minus(start))
    incomplete = rack().get_incomplete_cables()
    assert len(incomplete) == 1
    dragging = incomplete[0]
    RESULT["hovered_output"] = [
        dragging.hovered_output_port is out,
        dragging.get_hovered_port(Port.Type.OUTPUT) is out,
        dragging.hovered_input_port is None,
        dragging.get_hovered_port(Port.Type.INPUT) is None,
    ]
    state.handle_button(end, LEFT, RELEASE, 0)
    context = raw.context_get()
    assert context is not None and context.history is not None
    before_clear = list(_testing.rack_cables())
    history_index = context.history.action_index
    rack().clear_cables()
    RESULT["clear_cables"] = [before_clear, list(_testing.rack_cables()), context.history.action_index == history_index]
    start, end = scene_pos(out), scene_pos(inp)
    state.handle_hover(start, Vec())
    state.handle_button(start, LEFT, PRESS, 0)
    state.handle_hover(end, end.minus(start))
    incomplete = rack().get_incomplete_cables()
    assert len(incomplete) == 1
    dragging = incomplete[0]
    RESULT["hovered_cable"] = [
        dragging.hovered_input_port is inp,
        dragging.get_hovered_port(Port.Type.INPUT) is inp,
        dragging.hovered_output_port is None,
        dragging.get_hovered_port(Port.Type.OUTPUT) is None,
        dragging.output_plug.get_cable() is dragging,
        dragging.output_plug.get_type() == Port.Type.OUTPUT,
    ]
    # Call the Python bindings themselves, not only Rack's EventState dispatch.
    inp.on_drag_leave(w.DragLeaveEvent(button=LEFT, origin=out))
    left = dragging.hovered_input_port is None
    inp.on_drag_enter(w.DragEnterEvent(button=LEFT, origin=out))
    entered = dragging.hovered_input_port is inp
    inp.on_drag_drop(w.DragDropEvent(button=LEFT, origin=out))
    RESULT["direct_port_drag"] = [left, entered, dragging.input_port is inp and dragging.is_complete()]
    state.handle_button(end, LEFT, RELEASE, 0)
    RESULT["cable"] = list(_testing.rack_cables())
    gate = b.module
    RESULT["connected"] = gate is not None and gate.inputs[0].is_connected()
    yield from cable_views(out, inp)
    inp.delete_top_cable_action()
    deleted = list(_testing.rack_cables())
    _testing.history_undo()
    restored_delete = list(_testing.rack_cables())
    rack().clear_cables_action()
    cleared = list(_testing.rack_cables())
    _testing.history_undo()
    restored_clear = list(_testing.rack_cables())
    context = raw.context_get()
    assert context is not None and context.history is not None
    history_index = context.history.action_index
    rack().clear_cables_on_port(inp)
    port_cleared = list(_testing.rack_cables())
    unchanged_history = context.history.action_index == history_index
    drag(scene_pos(out), scene_pos(inp))
    RESULT["cable_removals"] = [deleted, restored_delete, cleared, restored_clear, port_cleared, unchanged_history]

    # Rack's Delete, then undo and redo: Rack recreates the widget with the Python factory.
    removes = len(RESULT["removes"])
    RESULT["removed_id"] = first
    _testing.remove_module_widget(first)
    RESULT["removed"] = {
        "removes": RESULT["removes"][removes:],
        "widget": a.alive,
        "cables": list(_testing.rack_cables()),
    }
    _testing.history_undo()
    yield from wait(2)
    RESULT["undone"] = {"rebuilt": first in RESULT["factory"][-3:], "cables": list(_testing.rack_cables())}
    _testing.history_redo()
    RESULT["redone"] = list(_testing.rack_cables())
    _testing.history_undo()
    yield from wait(2)
    yield from actions(first)
    RESULT["ids"] = sorted(voices_alive())

    # Model's Python calls: Python owns what they make.
    model = raw.plugin.get_model("RackPy", "PyVoice")
    assert model is not None
    made = model.create_module()
    assert isinstance(made, Module)
    RESULT["create_module"] = [made.model is model, made.alive]
    shown = model.create_module_widget()
    assert isinstance(shown, app.ModuleWidget)  # this model has a Python widget factory
    RESULT["preview_call"] = [shown.module is None, shown.parent is None]
    shown.close()
    module_id = _testing.engine_adopt(made)
    owner = model.create_module_widget(made)
    assert isinstance(owner, app.ModuleWidget)
    try:
        model.create_module_widget(made)
        RESULT["second_owner"] = "allowed"
    except OwnershipError:
        RESULT["second_owner"] = "refused"
    try:
        app.ModuleWidget().set_module(made)
        RESULT["outside_factory"] = "allowed"
    except OwnershipError:
        RESULT["outside_factory"] = "refused"
    RESULT["owner_module"] = owner.module is made
    # RP-28D: a Python module widget into Rack's rack and out again.
    rack().add_module_at_mouse(owner)
    in_rack = [rack().get_module(module_id) is owner, owner.parent is not None]
    rack().remove_module(owner)
    RESULT["rack_add_remove"] = [*in_rack, owner.parent is None, owner.alive, _testing.engine_has_module(module_id)]
    owner.close()  # a module widget deletes its module with it
    RESULT["owner_closed"] = _testing.engine_has_module(module_id)
    RESULT["final_cables"] = list(_testing.rack_cables())

    # The documentation's example, through Rack's own add path.
    blink_id = _testing.browser_add_module("RackPy", "PyBlink")
    built = [
        o for o in gc.get_objects() if isinstance(o, module_widget.BlinkWidget) and o.alive and o.module is not None
    ]
    RESULT["blink"] = [
        len(built) == 1,
        _testing.module_model(blink_id),
        built[0].get_output(0) is not None if built else False,
    ]
    _testing.remove_module_widget(blink_id)
    RESULT["final_cables"] = list(_testing.rack_cables())


def rack() -> Any:
    context = raw.context_get()
    assert context is not None and context.scene is not None
    return context.scene.rack


def cable_views(out: app.PortWidget, inp: app.PortWidget) -> Frames:
    """RP-28D: the cable Rack made, through its host views."""
    yield from wait(1)
    cables = rack().get_complete_cables()
    cable = cables[0]
    plug = cable.get_plug(Port.Type.INPUT)
    found: dict[str, Any] = {
        "views": [type(cable).__name__, type(plug).__name__],
        "ports": [cable.input_port is inp, cable.output_port is out, cable.is_complete()],
        "plug": [plug.get_cable() is cable, plug.get_type() == Port.Type.INPUT, cable.input_plug is plug],
        "lookups": [
            rack().get_top_cable(inp) is cable,
            rack().get_cable(out, inp) is cable,
            rack().get_cables_on_port(out) == [cable],
            rack().get_complete_cables_on_port(inp) == [cable],
            rack().get_top_plug(inp) is plug,
            cable.get_port(Port.Type.OUTPUT) is out,
        ],
        "positions": cable.get_input_pos().is_finite() and cable.get_output_pos().is_finite(),
        "json": "color" in cable.merge_json({}),
    }
    colour = cable.color
    cable.color = raw.color.from_hex_string("#3060f0")
    cable.from_json({"color": "#3060f0"})
    found["color"] = raw.color.to_hex_string(cable.color)
    cable.color = colour
    RESULT["cable_views"] = found


def voices_alive() -> list[int]:
    return [i for i, mw in VOICES.items() if mw.alive and _testing.engine_has_module(i)]


def actions(module_id: int) -> Frames:
    """ModuleWidget's own actions on a Python module widget in Rack's rack."""
    mw = VOICES[module_id]
    module = mw.module
    assert module is not None
    found: dict[str, Any] = {}
    data = mw.to_json()
    found["to_json"] = [data.get("model"), data.get("plugin")]
    module.params[0].value = 9.0
    mw.from_json(data)  # back to the saved value
    found["from_json"] = module.params[0].value
    preset = Path(CONFIG["result"]).with_name("voice.vcvm")
    mw.save(str(preset))
    module.params[0].value = 1.0
    mw.load(str(preset))
    found["preset"] = [preset.is_file(), module.params[0].value]
    module.params[0].value = 2.0
    mw.load_action(str(preset))
    found["load_action"] = module.params[0].value
    _testing.history_undo()
    found["load_action_undo"] = module.params[0].value
    module.params[0].value = 5.0
    mw.save_template()
    found["template"] = mw.has_template()
    module.params[0].value = 2.0
    mw.load_template()
    found["template_loaded"] = module.params[0].value
    mw.clear_template()
    found["template_cleared"] = not mw.has_template()
    mw.save_template_dialog()  # Rack saves immediately when no template exists
    found["template_dialog_created"] = mw.has_template()
    mw.clear_template()
    if os.environ.get("RACKPY_DIALOG_STUB"):
        checks: list[bool] = []
        dialog_preset = preset.with_name("dialog-preset.vcvm")
        os.environ["RACKPY_DIALOG_FILE"] = ""
        mw.save_dialog()
        mw.load_dialog()
        checks.append(not dialog_preset.exists() and module.params[0].value == 5.0)
        os.environ["RACKPY_DIALOG_FILE"] = str(dialog_preset)
        mw.save_dialog()
        checks.append(dialog_preset.is_file())
        module.params[0].value = 3.0
        mw.load_dialog()
        checks.append(module.params[0].value == 5.0)
        _testing.history_undo()
        module.params[0].value = 5.0
        mw.save_template()
        os.environ["RACKPY_DIALOG_RESPONSE"] = "cancel"
        mw.clear_template_dialog()
        checks.append(mw.has_template())
        module.params[0].value = 3.0
        mw.save_template_dialog()
        mw.load_template()
        checks.append(module.params[0].value == 5.0)
        os.environ["RACKPY_DIALOG_RESPONSE"] = "accept"
        module.params[0].value = 4.0
        mw.save_template_dialog()
        mw.load_template()
        checks.append(module.params[0].value == 4.0)
        mw.clear_template_dialog()
        checks.append(not mw.has_template())
        module.params[0].value = 5.0
        found["dialog_checks"] = checks
    mw.copy_clipboard()
    module.params[0].value = 3.0
    found["paste"] = [mw.paste_clipboard_action(), module.params[0].value]
    found["paste_json"] = mw.paste_json_action(data)
    module.params[0].value = 7.0
    rack().deselect_all()
    rack().select(mw)
    if os.environ.get("RACKPY_DIALOG_STUB"):
        selection_path = preset.with_name("dialog-selection.vcvs")
        os.environ["RACKPY_DIALOG_FILE"] = ""
        rack().save_selection_dialog()
        rack().load_selection_dialog()
        found["dialog_checks"].append(not selection_path.exists())
        os.environ["RACKPY_DIALOG_FILE"] = str(selection_path)
        rack().save_selection_dialog()
        found["dialog_checks"].append(selection_path.is_file())
        count = len(rack().get_modules())
        rack().load_selection_dialog()
        found["dialog_checks"].append(len(rack().get_modules()) == count + 1)
        _testing.history_undo()
        found["dialog_checks"].append(len(rack().get_modules()) == count)
        rack().deselect_all()
        rack().select(mw)
    rack().reset_selection_action()
    found["selection_reset"] = module.params[0].value
    _testing.history_undo()
    found["selection_reset_undo"] = module.params[0].value
    # Retry the vanishingly rare case that Rack randomly picks the existing value.
    for _ in range(3):
        rack().randomize_selection_action()
        randomized = module.params[0].value
        _testing.history_undo()
        if randomized != 7.0:
            break
    found["selection_randomized"] = randomized
    found["selection_randomize_undo"] = module.params[0].value
    _testing.history_redo()
    found["selection_randomize_redo"] = module.params[0].value
    _testing.history_undo()
    before_selection_disconnect = list(_testing.rack_cables())
    rack().disconnect_selection_action()
    after_selection_disconnect = list(_testing.rack_cables())
    _testing.history_undo()
    found["selection_disconnect"] = [
        before_selection_disconnect,
        after_selection_disconnect,
        list(_testing.rack_cables()),
    ]
    before_selection_clone = len(rack().get_modules())
    rack().clone_selection_action(False)
    found["selection_clone_count"] = len(rack().get_modules()) - before_selection_clone
    _testing.history_undo()
    found["selection_clone_undo"] = len(rack().get_modules()) == before_selection_clone
    rack().deselect_all()
    rack().select(mw)
    selection_path = Path(CONFIG["result"]).with_name("selection.vcvs")
    rack().save_selection(str(selection_path))
    selection = json.loads(selection_path.read_text())
    found["selection_saved"] = [m["model"] for m in selection["modules"]]
    modules_before = len(rack().get_modules())
    rack().load_selection(str(selection_path))
    found["selection_loaded"] = len(rack().get_modules()) - modules_before
    rack().delete_selection_action()
    found["selection_delete_count"] = len(rack().get_modules()) - modules_before
    _testing.history_undo()
    found["selection_delete_undo"] = len(rack().get_modules()) - modules_before
    _testing.history_undo()
    found["selection_load_undo"] = len(rack().get_modules()) == modules_before
    rack().deselect_all()
    rack().select(mw)
    rack().copy_clipboard_selection()
    found["selection_pasted"] = rack().paste_clipboard_action()
    found["selection_paste_count"] = len(rack().get_modules()) - modules_before
    _testing.history_undo()
    found["selection_paste_undo"] = len(rack().get_modules()) == modules_before
    rack().paste_module_json_action(data)
    found["module_paste_count"] = len(rack().get_modules()) - modules_before
    _testing.history_undo()
    found["module_paste_undo"] = len(rack().get_modules()) == modules_before
    rack().deselect_all()
    mw.reset_action()
    found["reset"] = module.params[0].value
    mw.randomize_action()
    mw.bypass_action(True)
    found["bypassed"] = module.is_bypassed()
    mw.bypass_action(False)
    before = len(VOICES)
    mw.clone_action()  # Rack duplicates it with the Python factory, cables included
    yield from wait(2)
    found["cloned"] = len(VOICES) == before + 1
    mw.disconnect_action()
    found["disconnected"] = list(_testing.rack_cables())
    _testing.history_undo()
    found["reconnected"] = list(_testing.rack_cables())
    context = raw.context_get()
    assert context is not None and context.history is not None
    history = context.history
    before_disconnect = history.action_index
    mw.disconnect()
    found["plain_disconnect"] = [list(_testing.rack_cables()), history.action_index == before_disconnect]
    mw.create_context_menu()  # Rack's module menu; it closes when the next click lands elsewhere
    yield from wait(1)
    events().handle_button(Vec(2, 2), LEFT, PRESS, 0)
    events().handle_button(Vec(2, 2), LEFT, RELEASE, 0)
    yield from wait(2)
    RESULT["actions"] = found


def phase2() -> Frames:
    yield from wait(3)
    RESULT["reloaded"] = {
        "rebuilt": sorted(i for i in RESULT["factory"] if i is not None),
        "cables": list(_testing.rack_cables()),
    }


_running: list[Frames] = []


def finish() -> None:
    Path(CONFIG["result"]).write_text(json.dumps(RESULT))
    _testing.close_window()


def on_step(frame: int, handle: object) -> None:
    if frame < 3 or RESULT.get("done"):
        return
    try:
        if not _running:
            _running.append(phase1() if CONFIG["phase"] == 1 else phase2())
        next(_running[0])
    except StopIteration:
        RESULT["done"] = True
        finish()
    except BaseException:
        RESULT["error"] = traceback.format_exc()
        RESULT["done"] = True
        finish()


_testing.set_step_callback(on_step)
