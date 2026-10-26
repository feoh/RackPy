# SPDX-License-Identifier: GPL-3.0-or-later
"""What a call into Rack's DSP code costs from Python, per call and in bulk (RP-18).

Measured inside Rack, on Rack's main thread at plugin start-up. Each workload
runs several times; the medians, the raw runs and the sample counts are
written as JSON when --rackpy-call-cost names a file, and the host test adds
the machine's details. Timing on a shared machine is diagnostic, not a
performance claim (docs/performance.md).
"""

import array
import json
import math
import statistics
import sys
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest

import rackpy.dsp.filters
from rackpy.raw import dsp
from rackpy.raw.simd import float_4

N = 20000
RUNS = 7


def ns_per_sample(work: Callable[[], object]) -> list[float]:
    runs = []
    for _ in range(RUNS):
        start = time.perf_counter_ns()
        work()
        runs.append((time.perf_counter_ns() - start) / N)
    return runs


def test_call_cost(request: pytest.FixtureRequest) -> None:
    samples = array.array("f", [math.sin(k * 0.01) * 5 for k in range(N)])
    values = samples.tolist()
    out = array.array("f", samples)
    biquad = dsp.BiquadFilter()
    biquad.set_parameters(dsp.BiquadFilter.Type.LOWPASS, 0.1, 0.707, 1.0)
    slew = dsp.SlewLimiter()
    slew.set_rise_fall(1000.0, 1000.0)
    schmitt = dsp.SchmittTrigger()
    vectors = [float_4(x) for x in values[: N // 4]]

    def biquad_calls() -> None:
        process = biquad.process
        for x in values:
            process(x)

    def slew_calls() -> None:
        process = slew.process
        for x in values:
            process(1e-3, x)

    def schmitt_calls() -> None:
        process = schmitt.process
        for x in values:
            process(x)

    def cubic_calls() -> None:
        cubic = dsp.cubic
        for x in values:
            cubic(x)

    def vector_calls() -> None:  # N // 4 calls of 4 lanes: N samples
        process = dsp.TBiquadFilter_float_4().process
        for v in vectors:
            process(v)

    workloads: dict[str, Callable[[], object]] = {
        "BiquadFilter.process, per call": biquad_calls,
        "BiquadFilter, process_block": lambda: rackpy.dsp.filters.process_block(biquad, samples, out),
        "SlewLimiter.process, per call": slew_calls,
        "SlewLimiter, process_block": lambda: rackpy.dsp.filters.process_block(slew, samples, out, 1e-3),
        "SchmittTrigger.process, per call": schmitt_calls,
        "cubic, per call": cubic_calls,
        "TBiquadFilter[float_4].process, per call (4 samples each)": vector_calls,
    }
    results: dict[str, Any] = {}
    for name, work in workloads.items():
        work()  # warm up
        runs = ns_per_sample(work)
        results[name] = {"median_ns_per_sample": statistics.median(runs), "runs_ns_per_sample": runs}
    per_call = results["BiquadFilter.process, per call"]["median_ns_per_sample"]
    bulk = results["BiquadFilter, process_block"]["median_ns_per_sample"]
    assert bulk < per_call, "the native loop should beat a Python loop of calls"
    path = request.config.getoption("--rackpy-call-cost")
    if path:
        Path(path).write_text(
            json.dumps(
                {
                    "samples_per_run": N,
                    "runs": RUNS,
                    "python": sys.version,
                    "copies": "process_block reads in_ and writes out in place: no copies. A per-call "
                    "loop converts each Python float to a C++ float and the result back.",
                    "results": results,
                },
                indent=1,
            )
        )
