# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-24C: rack::history in Rack's live UI. The RuntimeProbe (one param) is the module the actions target."""

from collections.abc import Iterator
from pathlib import Path
from typing import Any

import pytest

from rackpy import raw
from rackpy.errors import OwnershipError
from rackpy.raw import history as h
from rackpy.raw.engine import Engine, EngineModule
from rackpy.raw.math import Vec


def engine() -> Engine:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    return context.engine


def state() -> h.State:
    context = raw.context_get()
    assert context is not None and context.history is not None
    return context.history


@pytest.fixture
def probe() -> Iterator[EngineModule]:
    found = [engine().get_module(i) for i in engine().get_module_ids()]
    probes = [
        m for m in found if isinstance(m, EngineModule) and m.model is not None and m.model.slug == "RuntimeProbe"
    ]
    assert probes
    yield probes[0]


class Recorder(h.ModuleAction):
    """A Python action: its undo and redo record what ran."""

    def __init__(self, log: list[str], tag: str = "") -> None:
        super().__init__()
        self.name = "record"
        self.log = log
        self.tag = tag

    def undo(self) -> None:
        self.log.append(f"undo{self.tag}")

    def redo(self) -> None:
        self.log.append(f"redo{self.tag}")


def test_actions_python_makes(probe: EngineModule) -> None:
    e = engine()
    move = h.ModuleMove()
    assert move.module_id == -1 and move.name == "move module" and not move.owned_by_rack
    move.old_pos, move.new_pos = Vec(1, 2), Vec(3, 4)
    assert (move.old_pos.x, move.new_pos.y) == (1, 4)

    add = h.ModuleAdd()
    assert add.model is None and add.module_j is None and add.name == "add module"
    add.module_j = {"id": 5}
    assert add.module_j == {"id": 5}
    add.module_j = None
    add.redo()  # Rack would call model->createModule() on a null model: RackPy does nothing
    add.model = probe.model
    assert add.model is not None and add.model.slug == "RuntimeProbe"
    add.pos = Vec(45, 380)
    assert (add.pos.x, add.pos.y) == (45, 380)
    assert isinstance(h.ModuleRemove(), h.ModuleAdd) and h.ModuleRemove().name == "remove module"
    # Rack's typedefs are the specializations (RP-42): ModuleRemove is InverseAction<ModuleAdd>.
    assert h.InverseAction[h.ModuleAdd] is h.ModuleRemove and h.InverseAction[h.CableAdd] is h.CableRemove

    value = e.get_param_value(probe, 0)
    change = h.ParamChange()
    change.module_id, change.param_id, change.old_value, change.new_value = probe.id, 0, 0.125, 0.875
    try:
        change.redo()
        assert e.get_param_value(probe, 0) == 0.875
        change.undo()
        assert e.get_param_value(probe, 0) == 0.125
        change.param_id = 7  # Rack would write past the module's params
        change.redo()
        assert e.get_param_value(probe, 0) == 0.125
    finally:
        e.set_param_value(probe, 0, value)

    bypass = h.ModuleBypass()
    bypass.module_id, bypass.bypassed = probe.id, True
    bypass.redo()
    assert probe.is_bypassed()
    bypass.undo()
    assert not probe.is_bypassed()

    module_change = h.ModuleChange()
    module_change.module_id = probe.id
    module_change.new_module_j = e.module_to_json(probe)
    module_change.redo()
    module_change.undo()  # no old JSON: RackPy does nothing (Rack would load null)
    assert module_change.old_module_j is None
    # Rack's undo and redo load the JSON into the module (RP-42): each one's param value lands.
    old_j, new_j = e.module_to_json(probe), e.module_to_json(probe)
    old_j["params"][0]["value"], new_j["params"][0]["value"] = 0.125, 0.75
    module_change.old_module_j, module_change.new_module_j = old_j, new_j
    assert module_change.old_module_j == old_j and module_change.new_module_j == new_j
    try:
        module_change.redo()
        assert e.get_param_value(probe, 0) == 0.75
        module_change.undo()
        assert e.get_param_value(probe, 0) == 0.125
    finally:
        e.set_param_value(probe, 0, value)

    cable = h.CableAdd()
    cable.cable_id = 11
    cable.input_module_id, cable.output_module_id = probe.id, probe.id
    cable.input_id, cable.output_id = 0, 3  # the probe has one output
    assert (cable.cable_id, cable.input_module_id, cable.output_module_id) == (11, probe.id, probe.id)
    assert (cable.input_id, cable.output_id) == (0, 3)
    before = e.get_num_cables()
    cable.redo()  # Rack would index the output past the module's
    assert e.get_num_cables() == before and not cable.is_cable(None)
    cable.color = raw.nanovg.rgb(10, 20, 30)
    assert cable.color.g == pytest.approx(20 / 255)

    log: list[str] = []
    rec = Recorder(log)
    rec.undo()
    rec.redo()
    assert log == ["undo", "redo"]


def test_complex_actions() -> None:
    log: list[str] = []
    batch = h.ComplexAction()
    assert batch.is_empty() and batch.actions == []
    first, second = Recorder(log, "1"), Recorder(log, "2")
    batch.push(first)
    batch.push(second)
    assert batch.actions == [first, second] and batch.actions[0] is first and first.owned_by_rack
    with pytest.raises(OwnershipError, match="already owns"):
        batch.push(first)
    with pytest.raises(OwnershipError, match="itself"):
        batch.push(batch)
    batch.undo()
    batch.redo()
    assert log == ["undo2", "undo1", "redo1", "redo2"]  # Rack undoes a batch backwards
    assert not batch.is_empty()


def test_inverse_action(probe: EngineModule) -> None:
    with pytest.raises(TypeError, match="class template"):
        h.InverseAction()
    with pytest.raises(TypeError, match="Action class"):
        h.InverseAction[int]
    inverse: type[Any] = h.InverseAction[h.ParamChange]
    assert inverse is h.InverseAction[h.ParamChange] and issubclass(inverse, h.ParamChange)
    e = engine()
    value = e.get_param_value(probe, 0)
    action: Any = inverse()
    assert isinstance(action, h.InverseAction) and isinstance(action, h.ParamChange)
    action.module_id, action.param_id, action.old_value, action.new_value = probe.id, 0, 0.25, 0.75
    try:
        action.undo()  # Rack's ParamChange redo
        assert e.get_param_value(probe, 0) == 0.75
        action.redo()
        assert e.get_param_value(probe, 0) == 0.25
    finally:
        e.set_param_value(probe, 0, value)
    log: list[str] = []

    class Mark(Recorder):
        def __init__(self) -> None:
            super().__init__(log)

    swapped: Any = h.InverseAction[Mark]()
    swapped.undo()
    swapped.redo()
    assert log == ["redo", "undo"]
    # Nested, as in C++, InverseAction<InverseAction<T>> is T again (RP-42): it used to recurse forever.
    twice: Any = h.InverseAction[h.InverseAction[Mark]]()
    twice.undo()
    twice.redo()
    assert log == ["redo", "undo", "undo", "redo"]
    native: Any = h.InverseAction[h.InverseAction[h.ParamChange]]()
    native.module_id, native.param_id, native.old_value, native.new_value = probe.id, 0, 0.25, 0.75
    try:
        native.redo()  # ParamChange's redo
        assert e.get_param_value(probe, 0) == 0.75
        native.undo()
        assert e.get_param_value(probe, 0) == 0.25
    finally:
        e.set_param_value(probe, 0, value)


def test_racks_history() -> None:
    history = state()
    context = raw.context_get()
    assert context is not None and context.history is history and history.alive
    log: list[str] = []
    rec = Recorder(log, "")
    rec.name = "python step"
    start = history.action_index
    history.push(rec)
    assert rec.owned_by_rack and history.action_index == start + 1 and history.actions[-1] is rec
    assert history.can_undo() and history.get_undo_name() == "python step"
    with pytest.raises(OwnershipError, match="already owns"):
        history.push(rec)
    history.set_saved()
    assert history.is_saved() and history.saved_index == history.action_index
    # Writable as in Rack (RP-42): saved_index with any int; action_index within 0..len(actions).
    history.saved_index = 9999
    assert history.saved_index == 9999 and not history.is_saved()
    history.set_saved()
    done = history.action_index
    with pytest.raises(ValueError, match="action_index"):
        history.action_index = len(history.actions) + 1
    with pytest.raises(ValueError, match="action_index"):
        history.action_index = -1
    history.action_index = done - 1
    assert history.action_index == done - 1 and history.can_redo() and history.get_redo_name() == "python step"
    history.action_index = done
    assert history.is_saved() and log == []  # moving the index runs nothing
    history.undo()
    assert log == ["undo"] and history.can_redo() and history.get_redo_name() == "python step"
    assert not history.is_saved()
    history.redo()
    assert log == ["undo", "redo"]

    class Pusher(h.Action):
        def undo(self) -> None:
            try:
                history.push(h.Action())
                log.append("pushed")
            except OwnershipError as error:
                log.append(f"refused: {error}")

    pusher = Pusher()
    pusher.name = "pusher"
    history.push(pusher)
    history.undo()  # Rack's State::push here would delete this running action
    assert log[-1].startswith("refused") and "running action" in log[-1]

    class Failing(h.Action):
        def undo(self) -> None:
            raise RuntimeError("reported, not raised")

    failing = Failing()
    failing.name = "failing"
    history.redo()
    history.push(failing)
    index = history.action_index
    history.undo()  # as from Rack's Edit menu: the error is reported
    assert history.action_index == index - 1

    # Pushing after an undo deletes the undone actions: Rack deletes Python's too.
    history.push(h.Action())
    with pytest.raises((ValueError, RuntimeError, TypeError)):
        _ = failing.name


def test_runnable_history_snippet(probe: EngineModule) -> None:
    path = Path(__file__).resolve().parents[3] / "docs" / "snippets" / "history_action.py"
    scope: dict[str, Any] = {}
    exec(compile(path.read_text(), str(path), "exec"), scope)
    e, history = engine(), state()
    value = e.get_param_value(probe, 0)
    try:
        scope["change_level"](probe, 0, 0.625)
        assert e.get_param_value(probe, 0) == 0.625 and history.get_undo_name() == "change level"
        history.undo()
        assert e.get_param_value(probe, 0) == value
        history.redo()
        assert e.get_param_value(probe, 0) == 0.625
    finally:
        e.set_param_value(probe, 0, value)
