# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack modules=PyControls,PyNotes
from typing import Any

from rackpy.engine import Module
from rackpy.plugin import plugin


def controls(module: Module) -> None:
    module.config(num_params=1, num_inputs=1, num_outputs=1)
    module.config_param(0, 0, 2, 1, "Level")
    module.config_input(0, "In")
    module.config_output(0, "Out")


def notes(module: Module) -> None:
    module.config(num_params=0, num_inputs=0, num_outputs=0)
    state: dict[str, Any] = {"schema": 1, "text": ""}

    def load(data: Any) -> None:
        if isinstance(data, dict) and isinstance(data.get("text"), str):
            state["text"] = data["text"]

    module.set_data_handlers(to_json=lambda: dict(state), from_json=load)


plugin.add_model("PyControls", controls, hp=4)
plugin.add_model("PyNotes", notes, hp=8)
assert [model.slug for model in plugin.raw.models] == ["PyControls", "PyNotes"]
