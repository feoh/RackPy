# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack modules=PyControls,PyNotes
from typing import Any

import rackpy.assets
from rackpy.engine import Module
from rackpy.plugin import ModuleClass, on_destroy, plugin


def controls(module: Module) -> None:
    module.config(num_params=1, num_inputs=1, num_outputs=1)
    module.config_param(0, 0, 2, 1, "Level")
    module.config_input(0, "In")
    module.config_output(0, "Out")


class Notes(ModuleClass):
    SCHEMA = 1

    def configure(self) -> None:
        self.module.config(num_params=0, num_inputs=0, num_outputs=0)
        self.text = ""

    def to_json(self) -> Any:
        return {"schema": self.SCHEMA, "text": self.text}

    def from_json(self, data: Any) -> None:
        if isinstance(data, dict) and isinstance(data.get("text"), str):
            self.text = data["text"]


plugin.add_model("PyControls", controls, hp=4)
plugin.add_model("PyNotes", Notes, hp=8)
assert [model.slug for model in plugin.raw.models] == ["PyControls", "PyNotes"]
assert rackpy.assets.plugin("plugin.json").is_file()


@on_destroy
def goodbye() -> None:
    pass  # Python-only cleanup; Rack's objects are gone by now
