# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-14: configuration and state survive Rack's own save and reload (live UI suite only).

Rack saves a module with Module::toJson and reloads it into a freshly
constructed module with Module::fromJson. Configuration is not saved: the
constructor rebuilds it. Python JSON overrides (dataToJson and friends) are RP-15.
"""

import _rackpy_host
from _rackpy_host import _testing as HOOKS

from rackpy.raw.engine import Module, ParamQuantity, Port, PortInfo


class Decibels(ParamQuantity):
    def get_label(self) -> str:
        return self.name + " (dB)"


def construct(m: Module) -> None:
    m.config(3, 1, 1, 0)
    m.config_param(0, 0, 10, 2, "Gain", " dB", 0, 2, 3, quantity_type=Decibels)
    m.config_switch(1, 0, 3, 1, "Mode", ["A", "B", "C", "D"])
    m.config_button(2, "Push")
    info = PortInfo()
    info.module, info.port_id, info.type, info.name = m, 0, Port.Type.INPUT, "Raw in"
    m.input_infos[0] = info
    m.config_bypass(0, 0)


def describe(m: Module) -> list[object]:
    gain, mode = m.get_param_quantity(0), m.get_param_quantity(1)
    assert isinstance(gain, Decibels)
    return [
        [p.value for p in m.params],
        m.is_bypassed(),
        gain.get_label(),
        gain.get_display_value(),
        mode.get_display_value_string(),
        m.get_input_info(0).get_name(),
        [(r.input_id, r.output_id) for r in m.bypass_routes],
    ]


def test_module_survives_rack_save_and_reload() -> None:
    saved = Module.create(construct)
    HOOKS.engine_adopt(saved)
    for p, value in zip(saved.params, (7, 3, 1), strict=True):
        p.value = value
    HOOKS.engine_bypass(saved, True)
    before = describe(saved)
    data = HOOKS.engine_module_to_json(saved)
    HOOKS.engine_delete(saved)
    assert data["bypass"] is True
    assert [(p["id"], p["value"]) for p in data["params"]] == [(0, 7), (1, 3), (2, 1)]

    reloaded = Module.create(construct)
    HOOKS.engine_adopt(reloaded)
    try:
        HOOKS.engine_module_from_json(reloaded, data)
        assert describe(reloaded) == before
        assert before[2:6] == ["Gain (dB)", 17, "D", "Raw in"]
    finally:
        HOOKS.engine_bypass(reloaded, False)
        HOOKS.engine_delete(reloaded)


def test_rack_saves_and_loads_custom_data_and_quantity_overrides() -> None:
    loaded: list[object] = []

    class Tagged(ParamQuantity):
        def to_json(self) -> dict[str, object]:
            return {"value": self.get_value(), "tag": "kept"}

    def build(m: Module) -> None:
        m.config(1, 0, 0, 0)
        m.config_param(0, 0, 10, 4, quantity_type=Tagged)
        m.set_data_handlers(to_json=lambda: {"mode": "warm"}, from_json=loaded.append)

    saved = Module.create(build)
    HOOKS.engine_adopt(saved)
    data = saved.to_json()  # through Rack's Engine::moduleToJson, as a save is
    assert data == HOOKS.engine_module_to_json(saved)
    HOOKS.engine_delete(saved)
    assert data["data"] == {"mode": "warm"}
    assert data["params"] == [{"value": 4, "tag": "kept", "id": 0}]

    reloaded = Module.create(build)
    HOOKS.engine_adopt(reloaded)
    try:
        reloaded.from_json(data)
        assert loaded == [{"mode": "warm"}]
        # Rack calls dataFromJson only when "data" is present; JSON null passes as None.
        reloaded.from_json({**data, "data": None})
        del data["data"]
        reloaded.from_json(data)
        assert loaded == [{"mode": "warm"}, None]
    finally:
        HOOKS.engine_delete(reloaded)


def test_handler_errors_during_rack_save_and_load_are_contained() -> None:
    def fail() -> object:
        raise RuntimeError("save handler failed")

    def fail_load(data: object) -> None:
        raise RuntimeError("load handler failed")

    def build(m: Module) -> None:
        m.config(1, 0, 0, 0)
        m.config_param(0, 0, 10, 4)
        m.set_data_handlers(to_json=fail, from_json=fail_load)

    m = Module.create(build)
    HOOKS.engine_adopt(m)
    try:
        raised = _rackpy_host.dispatch_stats()["raised"]
        data = HOOKS.engine_module_to_json(m)
        assert "data" not in data and data["params"] == [{"value": 4, "id": 0}]
        data["params"][0]["value"] = 9
        HOOKS.engine_module_from_json(m, {**data, "data": {"any": 1}})
        assert m.params[0].value == 9  # the rest of Rack's load happened
        assert _rackpy_host.dispatch_stats()["raised"] == raised + 2
    finally:
        HOOKS.engine_delete(m)
