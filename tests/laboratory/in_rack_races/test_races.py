# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22D crash/recursion probes; each test runs in its own Rack subprocess."""

import gc
import warnings
from typing import Any

import pytest

from rackpy import laboratory
from rackpy.errors import StaleHandleError
from rackpy.laboratory import Bench, ModuleClass, ProcessArgs, Quantity

laboratory.enable()


class Empty(ModuleClass):
    def configure(self) -> None:
        self.module.config(0, 1, 1, 0)


def test_override_exceptions() -> None:
    class Broken(Empty):
        def process(self, args: ProcessArgs) -> None:
            raise LookupError("process failure")

        def on_save(self, event: Any) -> None:
            raise ValueError("event failure")

        def data_to_json(self) -> Any:
            raise KeyError("JSON failure")

    class BrokenValue(Quantity):
        def get_value(self) -> float:
            raise ArithmeticError("quantity failure")

    bench = Bench()
    instance = Broken.create()
    bench.add(instance.module)
    for _ in range(3):
        with pytest.raises(LookupError, match="process failure"):
            bench.step()
        with pytest.raises(ValueError, match="event failure"):
            bench.prepare_save()
        with pytest.raises(KeyError, match="JSON failure"):
            instance.module.data_to_json()
        with pytest.raises(ArithmeticError, match="quantity failure"):
            BrokenValue().get_display_value_string()
        assert bench.frame == 0
    bench.clear()
    instance.module.close()


def test_recursion_through_super() -> None:
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)

        class Recursive(Empty):
            def process(self, args: ProcessArgs) -> None:
                super().process(args)  # Rack's base calls the virtual step().

            def step(self) -> None:
                self.process(ProcessArgs())

    bench = Bench()
    instance = Recursive.create()
    bench.add(instance.module)
    with pytest.raises(RecursionError, match="laboratory callbacks nested"):
        bench.step()
    assert bench.frame == 0
    # ErrorCapture and nesting must unwind: another module still works afterwards.
    bench.clear()
    bench.add(Empty.create().module)
    bench.step()
    assert bench.frame == 1


def test_recursion_between_objects() -> None:
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)

        class Partner(Empty):
            peer: Partner

            def configure(self) -> None:
                super().configure()
                self.calls = 0

            def process(self, args: ProcessArgs) -> None:
                super().process(args)

            def step(self) -> None:
                self.calls += 1
                self.peer.process(ProcessArgs())

    a, b = Partner.create(), Partner.create()
    a.peer, b.peer = b, a
    bench = Bench()
    bench.add(a.module)
    bench.add(b.module)
    with pytest.raises(RecursionError, match="laboratory callbacks nested"):
        bench.step()
    assert bench.frame == 0
    assert a.calls > 0 and b.calls > 0
    bench.clear()
    a.module.close()
    b.module.close()


def test_stale_after_close_and_delete() -> None:
    for explicit in (True, False):
        instance = Empty.create()
        bench = Bench()
        bench.add(instance.module)
        port = instance.module.outputs[0]
        voltages = port.voltages
        bench.clear()
        if explicit:
            instance.module.close()
            with pytest.raises(StaleHandleError):
                instance.process(ProcessArgs())
            with pytest.raises(StaleHandleError):
                bench.add(instance.module)
        del instance
        gc.collect()
        assert not port.alive
        with pytest.raises(StaleHandleError):
            port.get_voltage()
        with pytest.raises(StaleHandleError):
            _ = voltages[0]
