# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-16A test entry module: three Python models, then Rack's own code paths from the probe's step."""

import json
import traceback
from pathlib import Path
from typing import Any

import _rackpy_host
from _rackpy_host import _testing

from rackpy.engine import Module
from rackpy.plugin import ModuleClass, plugin

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "rp16_config.json").read_text())
RESULT: dict[str, Any] = {"created": []}


def alpha(m: Module) -> None:
    m.config(2, 1, 1, 0)
    m.config_param(0, 0, 10, 5, "Level")
    m.config_param(1, 0, 1, 0, "Mode")
    state: dict[str, Any] = {"schema": 1, "notes": []}

    def load(data: Any) -> None:
        if isinstance(data, dict) and data.get("schema") == 1 and isinstance(data.get("notes"), list):
            state["notes"] = [n for n in data["notes"] if isinstance(n, str)]

    m.set_data_handlers(to_json=lambda: dict(state), from_json=load)
    RESULT["created"].append("PyAlpha")


def beta(m: Module) -> None:
    m.config(1, 0, 0, 0)
    m.config_param(0, -1, 1, 0, "Offset")
    RESULT["created"].append("PyBeta")


def broken(m: Module) -> None:
    if CONFIG.get("broken"):
        raise RuntimeError("rp16-intentional-factory-error")
    m.config(1, 0, 0, 0)
    m.config_param(0, 0, 1, 0.5, "Kept")
    m.set_data_handlers(to_json=lambda: {"kept": True})
    RESULT["created"].append("PyBroken")


class Classy(ModuleClass):
    def configure(self) -> None:
        self.module.config(1, 0, 0, 0)
        self.module.config_param(0, 0, 1, 0, "Depth")
        self.words: list[str] = []
        RESULT["created"].append("PyClassy")

    def to_json(self) -> Any:
        return {"words": self.words}

    def from_json(self, data: Any) -> None:
        if isinstance(data, dict) and isinstance(data.get("words"), list):
            self.words = [w for w in data["words"] if isinstance(w, str)]


plugin.add_model("PyAlpha", alpha, hp=6)
plugin.add_model("PyBeta", beta)
plugin.add_model("PyBroken", broken)
plugin.add_model("PyClassy", Classy)
RESULT["staged"] = [model.slug for model in plugin.raw.models]


def handle(module_id: int) -> Module:
    module = _testing.engine_handle(module_id)
    assert module is not None, f"no module {module_id}"
    return module


def first(ids: dict[str, int]) -> None:
    for slug in ("PyAlpha", "PyBeta", "PyBroken", "PyClassy"):
        ids[slug] = _testing.browser_add_module("RackPy", slug)
    alpha_module, beta_module, broken_module = (handle(ids[s]) for s in ("PyAlpha", "PyBeta", "PyBroken"))
    alpha_module.params[0].value = 7.5
    alpha_module.data_from_json({"schema": 1, "notes": ["hello", "λ"]})
    beta_module.params[0].value = -0.25
    broken_module.params[0].value = 0.75
    classy_module = handle(ids["PyClassy"])
    classy_module.data_from_json({"words": ["one", 2, "three"]})
    RESULT["preview"] = _testing.model_preview("RackPy", "PyAlpha")
    RESULT["models"] = {slug: _testing.module_model(i) for slug, i in ids.items()}
    _testing.remove_module_widget(ids["PyBeta"])  # the module menu's Delete, undoable
    RESULT["beta_after_remove"] = _testing.engine_has_module(ids["PyBeta"])
    _testing.history_undo()
    RESULT["beta_after_undo"] = _testing.engine_has_module(ids["PyBeta"])
    restored = handle(ids["PyBeta"])
    RESULT["beta_value_after_undo"] = restored.params[0].value


def second(ids: dict[str, int]) -> None:
    alpha_module = handle(ids["PyAlpha"])
    RESULT["alpha"] = [alpha_module.params[0].value, alpha_module.data_to_json()]
    beta_module = handle(ids["PyBeta"])
    RESULT["beta"] = beta_module.params[0].value
    RESULT["broken"] = _testing.module_model(ids["PyBroken"])
    classy_module = handle(ids["PyClassy"])
    RESULT["classy"] = classy_module.data_to_json()
    RESULT["diagnosed"] = any(
        "rp16-intentional-factory-error" in message for *_, message in _rackpy_host.diagnostics(0)
    )


done = [False]


def on_step(frame: int, handle: object) -> None:
    if done[0] or frame < 3:
        return
    done[0] = True
    ids: dict[str, int] = dict(CONFIG.get("ids", {}))
    try:
        (first if CONFIG["phase"] == 1 else second)(ids)
        RESULT["ids"] = ids
    except BaseException:
        RESULT["error"] = traceback.format_exc()
    finally:
        Path(CONFIG["result"]).write_text(json.dumps(RESULT))
        _testing.close_window()


_testing.set_step_callback(on_step)
