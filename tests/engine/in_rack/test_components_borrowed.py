# SPDX-License-Identifier: GPL-3.0-or-later
"""Components a Rack module owns, seen from Python while the engine runs.

These run from the UI thread of windowed Rack (tests/engine/test_components.py),
on a RuntimeProbe module whose process() writes its output (param * 10 V) and
light (param) on the engine thread.
"""

import threading
import time
from typing import Any

import pytest

from rackpy.errors import StaleHandleError, ThreadError
from rackpy.raw.engine import Input, Light, Output, Param

try:
    from _rackpy_host import _testing
except ImportError:
    _testing = None  # type: ignore[assignment]


def components(probe: Any) -> tuple[Param, Input, Output, Light]:
    assert _testing is not None
    param, inp, out, light = _testing.probe_components(probe)
    return param, inp, out, light


def test_borrowed_components_are_the_modules_and_keep_identity(probe: Any) -> None:
    param, inp, out, light = components(probe)
    assert isinstance(inp, Input) and isinstance(out, Output)
    assert components(probe)[2] is out  # one Python object per component
    param.set_value(0.25)
    time.sleep(0.05)  # let the engine process a few blocks
    assert out.get_voltage() == pytest.approx(2.5) and light.get_brightness() == pytest.approx(0.25)


def test_values_from_the_engine_are_never_torn(probe: Any) -> None:
    param, _inp, out, _light = components(probe)
    seen = set()
    for i in range(20000):
        if i % 2000 == 0:
            param.set_value((i // 2000) / 10)
        seen.add(round(out.get_voltage(), 4))
    # Every read is a value the engine wrote: param * 10 for some param we set.
    assert seen <= {round(k * 1.0, 4) for k in range(10)} | {round(k / 10 * 10, 4) for k in range(10)}


def test_views_cannot_outlive_their_module(probe: Any) -> None:
    assert _testing is not None
    param, inp, out, light = components(probe)
    voltages = out.voltages
    with pytest.raises(BufferError):
        memoryview(voltages)  # a zero-copy view could outlive the module: refused, not copied
    snapshot = voltages.snapshot()  # copies are fine
    _testing.remove_module(probe)
    for stale in (
        param.get_value,
        inp.get_voltage,
        lambda: out.voltages[0],
        lambda: voltages[0],
        light.get_brightness,
        lambda: out.plug_lights,
    ):
        with pytest.raises(StaleHandleError, match="module that Rack has deleted"):
            stale()
    assert not out.alive and len(snapshot) == 16


def test_rack_owned_components_are_main_thread_only(probe: Any) -> None:
    _param, _inp, out, _light = components(probe)
    outcome: list[BaseException | float] = []

    def worker() -> None:
        try:
            outcome.append(out.get_voltage())
        except BaseException as e:
            outcome.append(e)

    t = threading.Thread(target=worker)
    t.start()
    t.join()
    assert isinstance(outcome[0], ThreadError)
