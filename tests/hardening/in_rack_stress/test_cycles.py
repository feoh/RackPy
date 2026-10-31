# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38: 1,000 create/delete and 1,000 save/reload cycles in live Rack, with the engine running.

Modules are made as Rack's module browser makes them (model, engine, widget, an
undoable add) and deleted with the module menu's Delete. Rack keeps at most 500 undo
actions, so the history is full after 250 cycles: growth is measured from cycle 300
to the end. RackPy's own counters (live references, transferred objects, deferred
releases, identity entries) must come back exactly; resident memory may move only
by allocator noise, far below what one leaked module a cycle would add.
"""

import gc
import os
import random
from pathlib import Path
from typing import Any

import _rackpy_host
import stress_models as sm
from _rackpy_host import _testing as HOOKS

from rackpy.engine import Module

CYCLES, WARM = 1000, 300
RSS_SLACK = 8 << 20  # bytes: 700 leaked modules with widgets and kernels would be tens of megabytes


def rss() -> int:
    return int(Path("/proc/self/statm").read_text().split()[1]) * os.sysconf("SC_PAGE_SIZE")


def snapshot() -> dict[str, int]:
    gc.collect()
    return {
        "rss": rss(),
        "native_references": _rackpy_host.native_references(),
        "transferred_objects": _rackpy_host.transferred_objects(),
        "pending_releases": _rackpy_host.pending_releases(),
        "identity_entries": _rackpy_host.identity_entries(),
        "engine_modules": HOOKS.engine_module_count(),
        "python_objects": len(gc.get_objects()),
    }


def check_growth(name: str, start: dict[str, int], end: dict[str, int]) -> None:
    print(f"{name}: cycle {WARM} {start}; cycle {CYCLES} {end}")
    exact = ("native_references", "transferred_objects", "pending_releases", "identity_entries", "engine_modules")
    assert {k: end[k] for k in exact} == {k: start[k] for k in exact}
    assert end["python_objects"] - start["python_objects"] < 200, (start, end)
    if "ASAN_OPTIONS" not in os.environ:  # AddressSanitizer's quarantine holds freed memory on purpose
        assert end["rss"] - start["rss"] < RSS_SLACK, (start, end)


def add() -> tuple[int, Module, sm.Churn]:
    module_id = HOOKS.browser_add_module("RackPy", "Churn")
    module = HOOKS.engine_handle(module_id)
    assert module is not None
    return module_id, module, sm.Churn.of(module)


def test_create_delete_cycles() -> None:
    anchor_id, anchor, _ = add()  # a neighbour that stays, cabled to every new module
    start: dict[str, int] = {}
    try:
        for cycle in range(CYCLES):
            module_id, module, _ = add()
            cables = [HOOKS.engine_connect(anchor, 0, module, 0), HOOKS.engine_connect(module, 0, anchor, 1)]
            module.params[0].value = (cycle % 9) / 4 - 1
            for cable in cables:  # Rack's Delete removes cable widgets; these engine cables have none
                HOOKS.engine_disconnect(cable)
            HOOKS.remove_module_widget(module_id)
            assert not HOOKS.engine_has_module(module_id)
            if cycle == WARM - 1:
                start = snapshot()
        check_growth("create/delete", start, snapshot())
    finally:
        HOOKS.remove_module_widget(anchor_id)


def state(rng: random.Random) -> dict[str, Any]:
    return {"steps": [rng.uniform(-5, 5) for _ in range(sm.STEPS)], "label": rng.choice(["", "ünï", "x" * 300])}


def test_save_reload_cycles() -> None:
    rng = random.Random(3805)
    start: dict[str, int] = {}
    for cycle in range(CYCLES):
        module_id, module, churn = add()
        saved = state(rng)
        churn.replace_state(saved)
        module.params[0].value = rng.uniform(-2, 2)
        HOOKS.engine_prepare_save()  # Rack's onSave, as before a patch save: patch storage is written
        stored = module.patch_storage(create=False) / sm.STORAGE
        assert stored.is_file()
        if cycle % 2:
            data: Any = HOOKS.engine_module_to_json(module)
        else:
            data = HOOKS.engine_module_to_json_text(module)  # Rack's own serializer, as a patch file holds it
        HOOKS.remove_module_widget(module_id)
        new_id, again, reloaded = add()
        if cycle % 2:
            HOOKS.engine_module_from_json(again, data)
        else:
            HOOKS.engine_module_from_json_text(again, data)
        assert reloaded.state["label"] == saved["label"]
        assert reloaded.state["steps"] == [float(v) for v in saved["steps"]]
        assert again.params[0].value == module_param(data)
        HOOKS.remove_module_widget(new_id)
        if cycle == WARM - 1:
            start = snapshot()
    check_growth("save/reload", start, snapshot())


def module_param(data: object) -> float:
    import json

    parsed = json.loads(data) if isinstance(data, str) else data
    assert isinstance(parsed, dict)
    return float(parsed["params"][0]["value"])
