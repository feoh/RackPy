# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from typing import Any

from rackpy.engine import Module


def build(module: Module) -> None:
    module.config(num_params=1, num_inputs=0, num_outputs=0)
    module.config_param(0, 0, 10, 5, "Level")
    state: dict[str, Any] = {"mode": "warm", "history": [1, 2, 3]}

    def save() -> dict[str, Any]:
        return {"mode": state["mode"], "history": state["history"]}

    def load(data: Any) -> None:
        if isinstance(data, dict) and data.get("mode") in {"warm", "cold"}:
            state["mode"] = data["mode"]  # ignore anything unknown or malformed

    module.set_data_handlers(to_json=save, from_json=load)


module = Module.create(build)
try:
    saved = module.data_to_json()
    assert saved == {"mode": "warm", "history": [1, 2, 3]}
    module.data_from_json({"mode": "cold", "future_field": True})
    module.data_from_json("not even an object")  # left alone by load()
    assert module.data_to_json()["mode"] == "cold"
finally:
    module.close()
