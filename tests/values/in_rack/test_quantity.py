# SPDX-License-Identifier: GPL-3.0-or-later
"""rack::Quantity and ValueQuantity inside Rack: UI overrides and off-thread fallback."""

import threading

import _rackpy_host
import pytest

from rackpy.raw import Quantity
from rackpy.raw.engine import ParamQuantity
from rackpy.runtime import diagnostics
from rackpy.types import ValueQuantity

try:
    from _rackpy_host import _testing
except ImportError:
    _testing = None  # type: ignore[assignment]

hooks = pytest.mark.skipif(_testing is None, reason="needs the test-hooks build (the production run covers the rest)")


def test_the_base_quantity_has_racks_defaults() -> None:
    q = Quantity()
    assert (q.get_value(), q.get_min_value(), q.get_max_value(), q.get_default_value()) == (0.0, 0.0, 1.0, 0.0)
    assert q.get_display_precision() == 5 and q.get_display_value_string() == "0" and q.get_string() == "0"
    assert q.get_label() == "" and q.get_unit() == "" and q.is_bounded() and q.get_range() == 1.0
    q.set_value(0.5)  # the base class stores nothing
    assert q.get_value() == 0.0


def test_value_quantity_stores_and_clamps_like_param_quantity() -> None:
    q = ValueQuantity(min_value=-1, max_value=1, default_value=0.5, label="Pan", unit=" %")
    assert q.get_value() == 0.5 and q.get_string() == "Pan: 0.5 %"
    q.set_value(3)
    assert q.get_value() == 1.0 and q.is_max()
    q.set_value(float("nan"))  # clampSafe: fmin(NaN, max) is max, so NaN becomes the maximum
    assert q.get_value() == 1.0
    q.set_display_value_string("0.25")
    assert q.get_value() == 0.25
    q.set_display_value_string("not a formula")  # unparseable: unchanged
    assert q.get_value() == 0.25
    q.label, q.unit, q.max_value = "Width", "", 4.0
    q.set_value(3)
    assert q.get_string() == "Width: 3" and q.get_range() == 5.0


def test_python_overrides_ui_methods() -> None:
    class Hertz(ValueQuantity):
        def get_display_value(self) -> float:
            return self.get_value() * 1000

        def get_unit(self) -> str:
            return " Hz"

        def get_label(self) -> str:
            return "Cutoff"

        def get_display_precision(self) -> int:
            return 3

    q = Hertz(0, 20, 0.44)
    # Rack's own getString() and getDisplayValueString() call the Python overrides.
    assert q.get_display_value_string() == "440" and q.get_string() == "Cutoff: 440 Hz"


def test_native_value_quantity_subclass_overrides_on_ui_thread() -> None:
    class Doubled(ValueQuantity):
        def get_value(self) -> float:
            return 2 * super().get_value()

    q = Doubled(0, 10, 2)
    assert q.get_range() == 10 and q.get_scaled_value() == pytest.approx(0.4)


def test_parameter_quantity_may_not_define_engine_thread_methods() -> None:
    with pytest.raises(TypeError, match="set_value, get_max_value"):

        class AlsoBad(ParamQuantity):
            def set_value(self, value: float) -> None: ...

            def get_max_value(self) -> float:
                return 2.0


def test_an_override_that_raises_propagates_to_python_callers() -> None:
    class Broken(ValueQuantity):
        def get_label(self) -> str:
            raise ValueError("deliberate")

    with pytest.raises(ValueError, match="deliberate"):
        Broken().get_string()


@hooks
def test_an_override_that_raises_never_reaches_rack() -> None:
    assert _testing is not None

    class Broken(ValueQuantity):
        def get_label(self) -> str:
            raise ValueError("deliberate in Rack")

    before = _rackpy_host.dispatch_stats()["raised"]
    q = Broken(0, 1, 0.5, "", "")
    # Rack calls getString() without the GIL: the error is caught at the boundary and Rack's base answers.
    assert _testing.quantity_string_as_rack(q) == "0.5"
    assert _rackpy_host.dispatch_stats()["raised"] == before + 1
    assert any(d.code == "callback_raised" and "deliberate in Rack" in d.message for d in diagnostics())


@hooks
def test_engine_thread_reads_never_touch_python() -> None:
    assert _testing is not None

    class Labelled(ValueQuantity):
        def get_label(self) -> str:
            return "Python"

    q = Labelled(0, 10, 2.5, "", "")
    before = _rackpy_host.dispatch_stats()
    assert _testing.read_quantity_on_rack_thread(q, 10000) == 2.5
    after = _rackpy_host.dispatch_stats()
    assert after["calls"] == before["calls"]  # never enters Python on Rack's thread
    assert after["refused_rack_thread"] == before["refused_rack_thread"] + 1  # bounded diagnostic per object


@hooks
def test_plain_quantity_off_thread_falls_back_without_python() -> None:
    assert _testing is not None
    calls: list[str] = []

    class Custom(Quantity):
        def get_value(self) -> float:
            calls.append("get")
            return 0.75

    q = Custom()
    before = _rackpy_host.dispatch_stats()
    assert _testing.read_quantity_on_rack_thread(q, 10) == 0.0  # Rack's Quantity base answer
    after = _rackpy_host.dispatch_stats()
    assert calls == [] and after["calls"] == before["calls"]
    assert after["refused_rack_thread"] == before["refused_rack_thread"] + 1
    assert any(d.code == "refused_rack_thread" and "Quantity.get_value" in d.message for d in diagnostics())


def test_value_quantity_is_safe_to_use_from_python_threads() -> None:
    q = ValueQuantity(0, 1000, 0)
    errors: list[BaseException] = []

    def worker() -> None:
        try:
            for i in range(2000):
                q.set_value(i % 1000)
                q.get_value()
                q.label = f"w{i}"
                q.get_label()
        except BaseException as e:
            errors.append(e)

    threads = [threading.Thread(target=worker) for _ in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert errors == [] and 0 <= q.get_value() <= 1000 and q.label.startswith("w")


def test_reset_and_randomize_may_be_overridden() -> None:
    calls: list[str] = []

    class Tracked(ValueQuantity):
        def reset(self) -> None:
            calls.append("reset")
            super().reset()

    q = Tracked(0, 1, 0.25)
    q.set_value(1)
    q.reset()
    assert calls == ["reset"] and q.get_value() == 0.25
    q.randomize()
    assert 0.0 <= q.get_value() <= 1.0


def test_same_quantity_reentry_through_racks_get_string_and_super() -> None:
    seen: list[str] = []

    class Reentrant(ValueQuantity):
        def get_label(self) -> str:
            seen.append("label")
            if len(seen) == 1:
                assert self.get_string() == "Base: 0"
            return super().get_label()

    q = Reentrant(0, 1, 0, "Base", "")
    assert q.get_string() == "Base: 0" and seen == ["label", "label"]


def test_same_quantity_unbounded_reentry_is_a_python_error() -> None:
    class Recursive(ValueQuantity):
        def get_label(self) -> str:
            return self.get_string()

    with pytest.raises(RecursionError, match="overrides nested more than 64"):
        Recursive().get_string()
