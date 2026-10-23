# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-14C4: Rack's engine routes a bypassed module's ports (live UI suite only).

source.output[0] -> target.input[0], target bypass route 0 -> 0,
target.output[0] -> sink.input[0], so that target's output is connected.
Compared with Rack's processBypass on a plain module (the RP-14A fixture).
"""

import time
from collections.abc import Callable
from typing import Any

from _rackpy_host import _testing as HOOKS

from rackpy.raw.engine import Module


def adopted(configure: Callable[[Module], object]) -> Module:
    m = Module.create(configure)
    HOOKS.engine_adopt(m)
    return m


def wait_for(condition: Callable[[], bool], seconds: float = 5) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.01)
    return condition()


def target_config(m: Module) -> None:
    m.config(0, 1, 1)
    m.config_bypass(0, 0)


def route_through_engine(case: dict[str, Any]) -> None:
    n = case["input_channels"]
    source = adopted(lambda m: m.config(0, 0, 1))
    target = adopted(target_config)
    sink = adopted(lambda m: m.config(0, 1, 0))
    cables = []
    try:
        if n:  # zero channels: the target's input stays disconnected
            cables.append(HOOKS.engine_connect(source, 0, target, 0))
        cables.append(HOOKS.engine_connect(target, 0, sink, 0))
        out = source.outputs[0]
        if n:
            out.set_channels(n)
            for i in range(n):
                out.set_voltage(i * 0.25 - 2, i)
        HOOKS.engine_bypass(target, True)
        routed = target.outputs[0]
        expected = case["voltages"]
        assert wait_for(
            lambda: routed.voltages.tolist() == expected and routed.channel_count == case["output_channels"]
        )
        assert target.is_bypassed()
        # Enabled again: Rack clears the outputs, and this module's process does nothing.
        HOOKS.engine_bypass(target, False)
        if n:
            out.set_voltage(9)
        assert wait_for(lambda: routed.voltages.tolist()[0] == 0 and routed.channel_count == 1)
    finally:
        for cable in cables:
            HOOKS.engine_disconnect(cable)
        for m in (source, target, sink):
            HOOKS.engine_delete(m)


def test_engine_bypass_routes_match_rack_reference(reference: dict[str, Any]) -> None:
    for case in reference["bypass"]:
        route_through_engine(case)
