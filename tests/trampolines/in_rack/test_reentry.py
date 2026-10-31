# SPDX-License-Identifier: GPL-3.0-or-later
"""One native virtual reentry, nonvirtual super, and bounded recursion per dispatch family."""

from typing import Any

import _rackpy_host
import pytest
from _rackpy_host import _testing as testing

from rackpy.raw import Quantity, app, engine, history, midi, ui, widget
from rackpy.types import ValueQuantity

# name, native bound base, virtual, contained errors, virtual arguments
FAMILIES = [
    ("quantity", Quantity, "get_label", False, ()),
    ("value_quantity", ValueQuantity, "get_label", False, ()),
    ("param_quantity", engine.ParamQuantity, "get_label", False, ()),
    ("switch_quantity", engine.SwitchQuantity, "get_label", False, ()),
    ("quantity_json", engine.ParamQuantity, "to_json", False, ()),
    ("quantity_void", ValueQuantity, "reset", False, ()),
    ("quantity_get_display_value", ValueQuantity, "get_display_value", False, ()),
    ("quantity_set_display_value", ValueQuantity, "set_display_value", False, (0.25,)),
    ("quantity_get_display_precision", ValueQuantity, "get_display_precision", False, ()),
    ("quantity_get_display_value_string", ValueQuantity, "get_display_value_string", False, ()),
    ("quantity_set_display_value_string", ValueQuantity, "set_display_value_string", False, ("0.25",)),
    ("quantity_get_unit", ValueQuantity, "get_unit", False, ()),
    ("quantity_get_string", ValueQuantity, "get_string", False, ()),
    ("quantity_randomize", ValueQuantity, "randomize", False, ()),
    ("quantity_description", engine.ParamQuantity, "get_description", False, ()),
    ("port_info", engine.PortInfo, "get_name", False, ()),
    ("light_info", engine.LightInfo, "get_name", False, ()),
    ("history_undo", history.ModuleAction, "undo", False, ()),
    ("history_redo", history.ModuleAction, "redo", False, ()),
    ("widget_step", widget.Widget, "step", True, ()),
    ("widget_event", widget.Widget, "on_dirty", True, ()),
    ("widget_result", widget.Widget, "get_relative_zoom", True, (None,)),
    ("ui_menu", ui.MenuItem, "create_child_menu", True, ()),
    ("app_control", app.ParamWidget, "init_param_quantity", True, ()),
    ("midi_port", midi.Output, "get_device_ids", False, ()),
    ("probe_child", testing.ProbeChild, "on_removed", True, ()),
]


@pytest.mark.parametrize("family,base,method,contained,args", FAMILIES, ids=[x[0] for x in FAMILIES])
def test_same_object_native_reentry_and_super(
    family: str, base: Any, method: str, contained: bool, args: tuple[Any, ...]
) -> None:
    seen: list[int] = []
    bases: list[Any] = []

    def override(self: Any, *callback_args: Any) -> Any:
        seen.append(len(seen))
        result = getattr(super(derived, self), method)(*callback_args)
        bases.append(result)
        if len(seen) == 1:
            testing.call_virtual(self, method, *args)
        return result

    # Exercise precisely pybind11's same-frame-name / same first argument heuristic.
    override.__code__ = override.__code__.replace(co_name=method)
    derived: Any = type(f"Reentrant_{family}", (base,), {method: override})
    obj = derived()
    try:
        testing.call_virtual(obj, method, *args)
        assert seen == [0, 1]  # native dispatch reaches Python twice, not the recorder/base
        assert len(bases) == 2  # super did not bounce back into the override
        if method == "get_label":
            assert bases[0] == bases[1]
        if method == "get_relative_zoom":
            assert bases == [1.0, 1.0]
        if method == "to_json":
            assert bases == [{"value": 0.0}, {"value": 0.0}]
        if method == "create_child_menu":
            assert bases == [None, None]
        # Explicit Base.method(obj) means base semantics for Python aliases, too.
        if method not in ("on_dirty", "on_removed"):
            getattr(base, method)(obj, *args)
            assert seen == [0, 1]
    finally:
        if isinstance(obj, widget.Widget):
            obj.close()


@pytest.mark.parametrize("family,base,method,contained,args", FAMILIES, ids=[x[0] for x in FAMILIES])
def test_unbounded_native_reentry_stops_and_depth_is_restored(
    family: str, base: Any, method: str, contained: bool, args: tuple[Any, ...]
) -> None:
    calls = 0
    recursive = True

    def override(self: Any, *callback_args: Any) -> Any:
        nonlocal calls
        calls += 1
        if recursive:
            testing.call_virtual(self, method, *args)
        return getattr(super(derived, self), method)(*callback_args)

    override.__code__ = override.__code__.replace(co_name=method)
    derived: Any = type(f"Recursive_{family}", (base,), {method: override})
    obj = derived()
    before = _rackpy_host.dispatch_stats()["raised"]
    try:
        if contained:
            testing.call_virtual(obj, method, *args)
            assert _rackpy_host.dispatch_stats()["raised"] == before + 1
        else:
            with pytest.raises(RecursionError, match="overrides nested more than 64"):
                testing.call_virtual(obj, method, *args)
        assert calls == 64
        recursive = False
        testing.call_virtual(obj, method, *args)
        assert calls == 65  # no poisoned thread-local counter after exception/containment
    finally:
        if isinstance(obj, widget.Widget):
            obj.close()


def test_native_cpp_subclass_still_dispatches_virtually_from_python() -> None:
    q = testing.native_quantity()
    assert q.get_value() == Quantity.get_value(q) == 0.75
    assert q.get_label() == Quantity.get_label(q) == "native subclass"
    assert q.get_string() == "native subclass: 0.75"


def test_late_class_and_instance_override_resolution_has_no_negative_cache() -> None:
    class Patched(ValueQuantity):
        pass

    q: Any = Patched()
    assert q.get_string() == "0"
    Patched.get_label = lambda self: "class"  # type: ignore[method-assign]
    assert q.get_string() == "class: 0"
    q.get_label = lambda: "instance"
    assert q.get_string() == "instance: 0"


@pytest.mark.parametrize("method", ["draw", "draw_framebuffer"])
def test_drawing_reentry_super_and_contained_depth_limit(method: str) -> None:
    from rackpy.raw.math import Rect, Vec

    calls = 0
    bases = 0
    recursive = False
    base: Any = widget.Widget if method == "draw" else widget.FramebufferWidget

    def override(self: Any, *args: Any) -> None:
        nonlocal calls, bases
        calls += 1
        if recursive or calls == 1:
            testing.call_virtual(self, method, *args)
        getattr(super(derived, self), method)(*args)
        bases += 1

    override.__code__ = override.__code__.replace(co_name=method)
    derived: Any = type("Drawing", (base,), {method: override})
    obj = derived()
    frame = widget.FramebufferWidget() if method == "draw" else obj
    if method == "draw":
        frame.add_child(obj)
    obj.box = frame.box = Rect(Vec(), Vec(20, 20))
    try:
        frame.render()
        assert calls == bases == 2
        calls = bases = 0
        recursive = True
        before = _rackpy_host.dispatch_stats()["raised"]
        frame.render()
        assert calls == bases == 64
        assert _rackpy_host.dispatch_stats()["raised"] == before + 1
        recursive = False
        frame.render()
        assert calls == bases == 65
    finally:
        frame.close()


def test_reentry_during_override_attribute_lookup_is_bounded_too() -> None:
    lookups = 0

    class RecursiveLookup(ValueQuantity):
        def __getattribute__(self, name: str) -> Any:
            nonlocal lookups
            if name == "get_label":
                lookups += 1
                testing.call_virtual(self, name)
            return super().__getattribute__(name)

    # pybind11's defaulted getattr clears lookup errors (unlike errors from
    # invoking the resolved override). Preserve that policy: safe native
    # fallback, with bounded lookup frames, rather than unbounded recursion.
    assert testing.call_virtual(RecursiveLookup(), "get_label") == ""
    assert lookups == 64


def test_depth_is_shared_between_different_trampoline_families() -> None:
    calls: list[str] = []

    class Q(ValueQuantity):
        def get_label(self) -> str:
            calls.append("quantity")
            testing.call_virtual(action, "undo")
            return ""

    class Action(history.ModuleAction):
        def undo(self) -> None:
            calls.append("history")
            testing.call_virtual(quantity, "get_label")

    quantity, action = Q(), Action()
    with pytest.raises(RecursionError, match="overrides nested more than 64"):
        testing.call_virtual(quantity, "get_label")
    assert calls == ["quantity", "history"] * 32


@pytest.mark.parametrize("base", [engine.ParamQuantity, engine.SwitchQuantity])
@pytest.mark.parametrize(
    "method,args",
    [
        ("get_display_value", ()),
        ("set_display_value", (0.25,)),
        ("get_display_precision", ()),
        ("get_display_value_string", ()),
        ("set_display_value_string", ("0.25",)),
        ("get_label", ()),
        ("get_unit", ()),
        ("get_string", ()),
        ("reset", ()),
        ("randomize", ()),
        ("get_description", ()),
    ],
)
def test_native_quantity_ui_override_errors_propagate(base: Any, method: str, args: tuple[Any, ...]) -> None:
    def fail(self: Any, *values: Any) -> Any:
        raise ValueError("native quantity callback failure")

    fail.__code__ = fail.__code__.replace(co_name=method)
    cls: Any = type("Broken", (base,), {method: fail})
    with pytest.raises(ValueError, match="native quantity callback failure"):
        testing.call_virtual(cls(), method, *args)


def test_native_reentry_during_float_result_conversion_and_super() -> None:
    callbacks = 0
    conversions = 0

    class Number:
        def __float__(self) -> float:
            nonlocal conversions
            conversions += 1
            if conversions == 1:
                assert testing.call_virtual(quantity, "get_display_value") == 0.25
            return 0.25

    class Q(ValueQuantity):
        def get_display_value(self) -> Any:
            nonlocal callbacks
            callbacks += 1
            assert super().get_display_value() == 0.0
            return Number()

    quantity = Q()
    assert testing.call_virtual(quantity, "get_display_value") == 0.25
    assert callbacks == conversions == 2


def test_infinite_float_conversion_reentry_is_bounded_and_recovers() -> None:
    callbacks = 0
    conversions = 0
    recursive = True

    class Number:
        def __float__(self) -> float:
            nonlocal conversions
            conversions += 1
            if recursive:
                return float(testing.call_virtual(quantity, "get_display_value"))
            return 0.25

    class Q(ValueQuantity):
        def get_display_value(self) -> Any:
            nonlocal callbacks
            callbacks += 1
            return Number()

    quantity = Q()
    # pybind11's numeric caster clears conversion exceptions and retries once.
    # The exhaustion latch prevents a retry from starting a fresh recursive tree.
    with pytest.raises(TypeError, match="Unable to cast Python instance"):  # translated from RuntimeError (RP-38)
        testing.call_virtual(quantity, "get_display_value")
    assert callbacks == 64 and 64 <= conversions <= 128
    recursive = False
    assert testing.call_virtual(quantity, "get_display_value") == 0.25
    assert callbacks == 65
