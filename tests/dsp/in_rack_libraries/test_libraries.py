# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-17B: a plugin's own C++ kernel library, run by Rack's engine (live UI suite).

tests/dsp/test_runtime.py stages the example library and several broken ones;
only the example's types may appear.
"""

import time
from collections.abc import Callable

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy.dsp.kernel import kernel_types, load
from rackpy.raw.engine import Module


def until(condition: Callable[[], bool], seconds: float = 2.0) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.005)
    return condition()


def test_only_the_valid_library_registered_types() -> None:
    names = {t.name for t in kernel_types()}
    assert "example.Fold" in names
    for refused in ("wrongabi.Never", "throws.Partial", "badname.Good", "rackpy.Gain2"):
        assert refused not in names
    fold = load("example.Fold")
    assert fold.controls == ("gain", "limit") and [f[0] for f in fold.fields] == ["input", "output", "gain", "limit"]
    with pytest.raises(ValueError, match="limit must be positive"):
        fold(input=0, output=0, limit=0)  # the library's own exception, as ValueError


def test_rack_runs_the_library_kernel() -> None:
    fold = load("example.Fold")
    source = Module.create(lambda m: m.config(0, 0, 1, 0))

    def folder(m: Module) -> None:
        m.config(0, 1, 1, 0)
        m.set_kernel(fold(input=0, output=0, limit=5))

    m = Module.create(folder)
    HOOKS.engine_adopt(source)
    HOOKS.engine_adopt(m)
    cable = HOOKS.engine_connect(source, 0, m, 0)
    try:
        out = m.outputs[0]
        for given, folded in ((2.0, 2.0), (7.0, 3.0), (-7.0, -3.0), (12.0, -2.0)):
            source.outputs[0].set_voltage(given)

            def reached(expected: float = folded) -> bool:
                return out.get_voltage() == expected

            assert until(reached), (given, out.get_voltage())
        kernel = m.kernel
        assert kernel is not None
        kernel.set("gain", 2.0)  # 12 V * 2 = 24 V folds to 4 V
        assert until(lambda: out.get_voltage() == 4.0), out.get_voltage()
    finally:
        HOOKS.engine_disconnect(cable)
        HOOKS.engine_delete(m, None)
        HOOKS.engine_delete(source, None)
