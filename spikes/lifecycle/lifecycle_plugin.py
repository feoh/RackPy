# SPDX-License-Identifier: GPL-3.0-or-later
"""Trusted script shipped in the RP-06 lifecycle probe plugin. Spike code, not an API.

scenario.json in the plugin directory chooses the worker mode:
  none   - no Python worker thread
  safe   - a worker thread calls the engine with the GIL released (RackPy policy)
  unsafe - the same worker keeps the GIL while it waits for the engine lock
"""

import json
import threading
from collections import Counter
from pathlib import Path

import _rackpy_lifecycle as rp

SCENARIO = json.loads((Path(__file__).parent / "scenario.json").read_text())
MODE = SCENARIO.get("worker", "none")


class Callbacks:
    def __init__(self) -> None:
        self.stop = threading.Event()
        self.worker: threading.Thread | None = None
        self.churned: list[rp.ModuleHandle] = []
        self.kept: list[rp.ModuleHandle] = []
        self.stats: Counter[str] = Counter()

    def on_reset(self, handle: rp.ModuleHandle) -> None:
        self.stats["resets"] += 1
        if self.stats["resets"] > 1:
            return
        self.kept.append(handle)
        rp.log(f"on_reset role={rp.thread_role()} has_context={rp.thread_has_rack_context()}")
        try:
            rp.engine_module_count()
            rp.log("reentry from exclusive callback: allowed (UNEXPECTED)")
        except RuntimeError as e:
            rp.log(f"reentry from exclusive callback refused: {e}")

    def data_to_json(self, handle: rp.ModuleHandle) -> str:
        self.stats["data_to_json"] += 1
        return json.dumps({"saved_by": rp.thread_role(), "gain": handle.gain})

    def data_from_json(self, handle: rp.ModuleHandle, text: str) -> None:
        rp.log(f"data_from_json role={rp.thread_role()} text={text}")

    def on_churn(self, handle: rp.ModuleHandle) -> None:
        self.churned.append(handle)
        self.stats["churn_added"] += 1

    def on_scenario(self, what: str, handle: rp.ModuleHandle) -> None:
        rp.log(f"scenario {what} role={rp.thread_role()} mode={MODE}")
        if what == "start" and MODE in ("safe", "unsafe"):
            self.worker = threading.Thread(target=self.work, name="rackpy-worker")
            self.worker.start()
        elif what == "stop" and self.worker is not None:
            self.stop.set()
            self.worker.join(timeout=10)
            rp.log(
                f"worker joined alive={self.worker.is_alive()} calls={self.stats['worker_calls']} "
                f"stale={self.stats['worker_stale']} live_reads={self.stats['worker_live_reads']}"
            )

    def work(self) -> None:
        rp.log(f"worker role={rp.thread_role()} has_context={rp.thread_has_rack_context()}")
        release = MODE == "safe"
        while not self.stop.is_set():
            rp.engine_module_count(release_gil=release)
            self.stats["worker_calls"] += 1
            for h in list(self.churned):
                try:
                    h.gain  # noqa: B018 - the read is the probe
                    self.stats["worker_live_reads"] += 1
                except ReferenceError:
                    self.stats["worker_stale"] += 1
                    self.churned.remove(h)

    def on_shutdown(self) -> None:
        alive = self.worker.is_alive() if self.worker else False
        rp.log(f"shutdown worker_alive={alive} resets={self.stats['resets']} saves={self.stats['data_to_json']}")
        for h in self.kept:
            try:
                h.gain  # noqa: B018
                rp.log("stale handle readable after context deletion (UNEXPECTED)")
            except ReferenceError as e:
                rp.log(f"stale handle after context deletion: ReferenceError ({e})")
        try:
            rp.engine_module_count()
            rp.log("engine call after context deletion: allowed (UNEXPECTED)")
        except ReferenceError as e:
            rp.log(f"engine call after context deletion: ReferenceError ({e})")


rp.set_callbacks(Callbacks())
rp.log(f"lifecycle script loaded mode={MODE}")
