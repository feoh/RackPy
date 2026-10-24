# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-15D test entry module: plugin settings and a module's state through Rack's real save and open."""

import json
import traceback
from pathlib import Path
from typing import Any

from _rackpy_host import _testing

from rackpy.engine import Module
from rackpy.plugin import ModuleClass, plugin

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "rp15d_config.json").read_text())
RESULT: dict[str, Any] = {}
SETTINGS: dict[str, Any] = {"schema": 1, "launches": 0}
GLFW_KEY_S, RACK_MOD_CTRL = 83, 0x0002  # GLFW_MOD_CONTROL on Linux


def load_settings(data: Any) -> None:
    RESULT["settings_loaded"] = data
    if isinstance(data, dict) and type(data.get("launches")) is int:
        SETTINGS["launches"] = data["launches"]


plugin.set_settings_handlers(to_json=lambda: dict(SETTINGS), from_json=load_settings)


class Store(ModuleClass):
    def configure(self) -> None:
        self.module.config(1, 0, 0, 0)
        self.module.config_param(0, 0, 1, 0, "Amount")
        self.notes: list[str] = []

    def to_json(self) -> Any:
        return {"notes": self.notes}

    def from_json(self, data: Any) -> None:
        if isinstance(data, dict) and isinstance(data.get("notes"), list):
            self.notes = [n for n in data["notes"] if isinstance(n, str)]


plugin.add_model("PyStore", Store)


def handle(module_id: int) -> Module:
    module = _testing.engine_handle(module_id)
    assert module is not None, f"no module {module_id}"
    return module


def state(module: Module) -> list[Any]:
    table = module.patch_storage(create=False) / "table.txt"
    return [module.params[0].value, module.data_to_json(), table.read_text() if table.is_file() else None]


def first() -> None:
    module_id = _testing.browser_add_module("RackPy", "PyStore")
    RESULT["id"] = module_id
    module = handle(module_id)
    module.params[0].value = 0.25
    module.data_from_json({"notes": ["kept", "λ"]})
    (module.patch_storage() / "table.txt").write_text("1 2 3")
    RESULT["before"] = state(module)
    del module
    _testing.remove_module_widget(module_id)
    RESULT["deleted"] = _testing.engine_has_module(module_id)
    _testing.history_undo()  # Rack keeps the storage of a deleted module until the patch is saved
    RESULT["after_undo"] = state(handle(module_id))
    _testing.press_key(GLFW_KEY_S, RACK_MOD_CTRL)  # File > Save, to the .vcv Rack opened


def second() -> None:
    RESULT["reopened"] = state(handle(CONFIG["id"]))


done = [False]


def on_step(frame: int, _handle: object) -> None:
    if done[0] or frame < 3:
        return
    done[0] = True
    try:
        SETTINGS["launches"] += 1
        (first if CONFIG["phase"] == 1 else second)()
    except BaseException:
        RESULT["error"] = traceback.format_exc()
    finally:
        Path(CONFIG["result"]).write_text(json.dumps(RESULT))
        _testing.close_window()


_testing.set_step_callback(on_step)
