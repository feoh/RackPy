# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.engine import Module
from rackpy.types import JSON

SCHEMA = 2  # version 1 saved "mode" only; version 2 added "history"


def build(module: Module) -> None:
    module.config(num_params=1, num_inputs=0, num_outputs=0)
    module.config_param(0, 0, 10, 5, "Level")
    state: dict[str, JSON] = {"mode": "warm", "history": [1, 2, 3]}

    def save() -> JSON:
        return {"schema": SCHEMA, "mode": state["mode"], "history": state["history"]}

    def load(data: JSON) -> None:
        if not isinstance(data, dict):
            return  # malformed: keep the defaults
        schema = data.get("schema", 1)  # version 1 patches have no "schema"
        if type(schema) is not int or schema > SCHEMA:
            return  # from a newer version of this module: do not guess
        if data.get("mode") in {"warm", "cold"}:
            state["mode"] = data["mode"]
        history = data.get("history")
        if schema >= 2 and isinstance(history, list) and all(type(v) is int for v in history):
            state["history"] = history[-16:]

    module.set_data_handlers(to_json=save, from_json=load)


module = Module.create(build)
try:
    assert module.data_to_json() == {"schema": 2, "mode": "warm", "history": [1, 2, 3]}
    module.data_from_json({"mode": "cold"})  # version 1: migrated
    module.data_from_json({"schema": 2, "history": [4, "five"], "future_field": True})  # bad history ignored
    module.data_from_json({"schema": 3, "mode": "warm"})  # newer: left alone
    module.data_from_json("not even an object")
    assert module.data_to_json() == {"schema": 2, "mode": "cold", "history": [1, 2, 3]}
finally:
    module.close()
