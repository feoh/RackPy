# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare equivalent facade and raw MIDI message construction inside Rack.

This measures UI-thread convenience overhead, not the native audio path.
Times on a shared host are diagnostic and must not be treated as a guarantee.
"""

import json
import statistics
import sys
import time
from pathlib import Path

import pytest

from rackpy.midi import message
from rackpy.raw.midi import Message

COUNT = 20_000
RUNS = 7


def raw_message() -> Message:
    result = Message()
    result.bytes = [0x90, 60, 100]
    result.frame = -1
    return result


def facade_message() -> Message:
    return message(0x90, 60, 100, frame=-1)


def test_construction_cost(request: pytest.FixtureRequest) -> None:
    direct, wrapped = raw_message(), facade_message()
    assert direct.bytes == wrapped.bytes == [0x90, 60, 100]
    assert direct.frame == wrapped.frame == -1
    raw_message()
    facade_message()
    # Alternate order to avoid giving either workload a fixed warm-cache advantage.
    raw: list[float] = []
    facade: list[float] = []
    for i in range(RUNS):
        order = [(raw_message, raw), (facade_message, facade)]
        if i % 2:
            order.reverse()
        for work, results in order:
            start = time.perf_counter_ns()
            for _ in range(COUNT):
                work()
            results.append((time.perf_counter_ns() - start) / COUNT)
    path = request.config.getoption("--rackpy-facade-cost")
    if path:
        Path(path).write_text(
            json.dumps(
                {
                    "baseline": "construct raw Message, set bytes and frame",
                    "candidate": "rackpy.midi.message with identical bytes and frame",
                    "samples_per_run": COUNT,
                    "runs": RUNS,
                    "python": sys.version,
                    "clock": vars(time.get_clock_info("perf_counter")),
                    "raw_ns_per_message": raw,
                    "facade_ns_per_message": facade,
                    "raw_median_ns": statistics.median(raw),
                    "facade_median_ns": statistics.median(facade),
                },
                indent=2,
            )
            + "\n"
        )
