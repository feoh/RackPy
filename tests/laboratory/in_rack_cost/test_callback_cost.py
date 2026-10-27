# SPDX-License-Identifier: GPL-3.0-or-later
"""Diagnostic cost of the real Bench/trampoline; NOT a production audio benchmark."""

import json
import random
import statistics
import sys
import time
from pathlib import Path
from typing import TypedDict

import pytest

from rackpy import laboratory
from rackpy.engine import Module

FRAMES = 20000
REPETITIONS = 21
WARMUP = 2000
SEED = 22


class CostRun(TypedDict):
    repetition: int
    variant: str
    elapsed_ns: int


def test_callback_cost(request: pytest.FixtureRequest) -> None:
    laboratory.enable()

    class Base(laboratory.ModuleClass):
        def configure(self) -> None:
            self.module.config(0, 0, 0, 0)

    class Empty(Base):
        def process(self, args: laboratory.ProcessArgs) -> None:
            pass

    class Super(Base):
        def process(self, args: laboratory.ProcessArgs) -> None:
            super().process(args)

    instances = [Base.create(), Empty.create(), Super.create()]
    modules = [Module.create(lambda m: m.config(0, 0, 0, 0)), *(i.module for i in instances)]
    names = ["native_module_empty", "laboratory_base", "python_empty", "python_super"]
    benches = {name: laboratory.Bench(48000) for name in names}
    for name, module in zip(names, modules, strict=True):
        benches[name].add(module)
        benches[name].step(WARMUP)
    rng = random.Random(SEED)
    raw: list[CostRun] = []
    for repetition in range(REPETITIONS):
        order = names.copy()
        rng.shuffle(order)
        for name in order:
            start = time.perf_counter_ns()
            benches[name].step(FRAMES)
            elapsed = time.perf_counter_ns() - start
            raw.append({"repetition": repetition, "variant": name, "elapsed_ns": elapsed})
    results = {}
    for name in names:
        times = [int(r["elapsed_ns"]) / FRAMES for r in raw if r["variant"] == name]
        results[name] = {"median_ns_per_frame": statistics.median(times), "runs_ns_per_frame": times}
        assert benches[name].frame == WARMUP + FRAMES * REPETITIONS
    for bench in benches.values():
        bench.clear()
    for module in modules:
        module.close()
    data = {
        "note": "Offline main-thread Bench diagnostic on a shared desktop; NOT production real-time performance.",
        "python": sys.version,
        "clock": vars(time.get_clock_info("perf_counter")),
        "sample_rate": 48000,
        "channels": 0,
        "modules_per_bench": 1,
        "frames_per_run": FRAMES,
        "warmup_frames": WARMUP,
        "repetitions": REPETITIONS,
        "shuffle_seed": SEED,
        "baseline": (
            "native_module_empty: ordinary RackPy Module, no kernel, parameters, ports or cables, on the same Bench"
        ),
        "method": (
            "One Bench.step(20000) per timed run; native host loop invokes each virtual once per frame. "
            "GIL already held. Includes Bench scheduling and callback dispatch/ProcessArgs conversion, "
            "not only the Python body. No audio clock, worker threads, GIL contention or device deadlines measured."
        ),
        "raw_runs_in_execution_order": raw,
        "results": results,
    }
    Path(request.config.getoption("--laboratory-cost-out")).write_text(json.dumps(data, indent=2) + "\n")
