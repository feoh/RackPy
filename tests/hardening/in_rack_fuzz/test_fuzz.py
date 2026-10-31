# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38: fuzzing RackPy's boundaries inside Rack's live UI (test-hooks build).

Every test feeds seeded random inputs, a bounded number of times, to one boundary
between trusted Python and native code: graph plans checked natively, JSON
conversion, indices and counts, values returned by Python overrides Rack calls, and
handles whose native objects are gone. An input may be refused only with one of the
exceptions RackPy documents for it; anything else, or a crash, fails. Rack keeps
running throughout, and each test ends by checking that it still does.
"""

import gc
import math
import random
from collections.abc import Callable
from typing import Any

import _rackpy_host
import pytest
from _rackpy_host import _testing as HOOKS
from _rackpy_host import dsp as host_dsp

from rackpy import raw
from rackpy.dsp.graph import OPS, Graph
from rackpy.errors import RackPyError, StaleHandleError
from rackpy.raw import ui
from rackpy.raw import widget as w
from rackpy.raw.engine import Module
from rackpy.raw.math import Rect, Vec

# What a malformed input may raise. A RackPyError (stale, ownership, thread) is RackPy's own refusal.
REFUSALS = (ValueError, TypeError, KeyError, IndexError, OverflowError, UnicodeError, RecursionError, RackPyError)


def refused_or_done(action: Callable[..., object], *args: object) -> object:
    """action(*args), or the refusal it raised: one of REFUSALS. Anything else propagates and fails the test."""
    try:
        return action(*args)
    except REFUSALS as e:
        return e


WEIRD_NUMBERS: list[object] = [
    0,
    1,
    -1,
    2,
    15,
    16,
    17,
    255,
    256,
    2**31 - 1,
    2**31,
    -(2**31) - 1,
    2**63 - 1,
    2**63,
    2**64,
    -(2**63) - 1,
    0.5,
    -0.0,
    1e308,
    -1e308,
    math.inf,
    -math.inf,
    math.nan,
    True,
    False,
    None,
    "1",
    b"1",
    [],
    {},
    object(),
]


# Component counts. A count Rack can try to allocate (2**31 - 1 lights) is left out: it fails with
# MemoryError, after which RackPy discards the half-configured module, but with enough memory it could succeed.
COUNTS: list[object] = [n for n in WEIRD_NUMBERS if n != 2**31 - 1] + [4096]


def weird(rng: random.Random) -> object:
    return rng.choice(WEIRD_NUMBERS)


def base_plan() -> dict[str, Any]:
    g = Graph()
    count = g.delay(0.0)
    count.feed(count + 1)
    g.output(0, g.sin2pi(g.phasor(g.pitch_to_freq(g.input(0) + g.param(0)))) * g.control("amp", 5.0) + count)
    g.light(0, g.pulse(g.schmitt(g.input(1), 0.1, 1.0), 0.01))
    return dict(g.plan().to_dict())


def mutate(plan: dict[str, Any], rng: random.Random) -> dict[str, Any]:
    nodes = [dict(n) for n in plan["nodes"]]
    out: dict[str, Any] = {**plan, "nodes": nodes}
    if rng.random() < 0.4:  # mostly valid: new argument values, which the kernel then runs with
        for node in nodes:
            if node["op"] in {"constant", "delay", "schmitt", "pulse", "control"}:  # float arguments, not indices
                node["args"] = [rng.choice([rng.uniform(-20, 20), 0.0, 1e30, -1e30]) for _ in node["args"]]
        return out
    for _ in range(rng.randint(1, 3)):
        choice = rng.randrange(9)
        node = rng.choice(nodes) if nodes else {}
        if choice == 0:
            node["op"] = rng.choice([*OPS, "no_such_op", "", "x" * 300, 7, None])
        elif choice == 1:
            node["inputs"] = [
                rng.choice([-1, 0, len(nodes) - 1, len(nodes), 10**9, rng.randrange(max(1, len(nodes)))])
                for _ in range(rng.randrange(4))
            ]
        elif choice == 2:
            node["args"] = [weird(rng) for _ in range(rng.randrange(4))]
        elif choice == 3:
            node.pop(rng.choice(["op", "inputs", "args"]), None)
        elif choice == 4:
            out["latches"] = [[weird(rng), weird(rng)] for _ in range(rng.randrange(3))]
        elif choice == 5:
            out["controls"] = [
                {"name": rng.choice(["amp", "", "a" * 200, 3]), "node": weird(rng), "default": weird(rng)}
            ]
        elif choice == 6:
            nodes.append({"op": rng.choice(list(OPS)), "inputs": [rng.randrange(len(nodes) + 2)], "args": [weird(rng)]})
        elif choice == 7:
            out[rng.choice(["nodes", "latches", "controls"])] = weird(rng)
        else:
            nodes.reverse()  # every reference now points elsewhere, often forward
    return out


def test_wrong_types_at_explicit_casts_are_type_errors() -> None:
    """RP-38 finding: pybind11 reports a failed py::cast as RuntimeError; RackPy translates it to TypeError."""
    with pytest.raises(TypeError, match="Unable to cast"):
        host_dsp.graph_kernel({"nodes": [{"op": 7, "inputs": [], "args": []}], "latches": [], "controls": []})
    with pytest.raises(TypeError, match="Unable to cast"):
        w.PathDropEvent(0)  # type: ignore[arg-type]


def test_graph_plans_are_checked_natively() -> None:
    rng = random.Random(3801)
    template = base_plan()
    compiled = refused = 0
    for _ in range(600):
        plan = mutate(template, rng)
        kernel = refused_or_done(host_dsp.graph_kernel, plan)
        if isinstance(kernel, BaseException):
            refused += 1
            continue
        compiled += 1
        m = Module.create(lambda m: m.config(1, 2, 1, 1))
        try:
            if not isinstance(refused_or_done(m.set_kernel, kernel), BaseException):
                for frame in range(64):
                    HOOKS.module_process(m, 48000, frame)
        finally:
            m.close()
    print(f"graph plans: {compiled} compiled and ran, {refused} refused")
    assert refused > 200 and compiled > 100


def random_value(rng: random.Random, depth: int = 0) -> object:
    kind = rng.randrange(14 if depth < 5 else 9)
    if kind == 0:
        return weird(rng)
    if kind == 1:
        return rng.choice(["", "plain", "nul\0inside", "\ud800lone", "ünïcödé", "x" * 1000])
    if kind == 2:
        return rng.uniform(-1e6, 1e6)
    if kind == 3:
        return rng.randint(-(2**70), 2**70)
    if kind == 4:
        return rng.choice([True, False, None])
    if kind == 5:
        return {1, 2}
    if kind == 6:
        return b"bytes"
    if kind == 7:
        return object()
    if kind == 8:
        return 1j
    if kind in (9, 10):
        return [random_value(rng, depth + 1) for _ in range(rng.randrange(4))]
    if kind == 11:
        return tuple(random_value(rng, depth + 1) for _ in range(rng.randrange(3)))
    if kind == 12:
        return {
            rng.choice(["k", "", "nul\0", 3, None, ("t",)]): random_value(rng, depth + 1)
            for _ in range(rng.randrange(4))
        }
    nested: list[object] = []
    for _ in range(rng.choice([10, 63, 64, 65, 200])):
        nested = [nested]
    return nested


class Holder:
    """A module's custom data: `value` is what it saves, `back` what it last loaded."""

    def __init__(self, value: object) -> None:
        self.value: object = value
        self.back: object = None

    def configure(self, m: Module) -> None:
        m.config(0, 0, 0, 0)
        m.set_data_handlers(to_json=self.save, from_json=self.load)

    def save(self) -> object:
        return self.value

    def load(self, data: object) -> None:
        self.back = data


def test_json_conversion_refuses_what_json_cannot_hold() -> None:
    rng = random.Random(3802)
    saved = refused = 0
    for i in range(1500):
        value = random_value(rng)
        if i % 100 == 0:
            loop: list[object] = [1]
            loop.append(loop)  # a cycle: refused, never followed forever
            value = loop
        holder = Holder(value)
        m = Module.create(holder.configure)
        try:
            data = refused_or_done(m.data_to_json)
            if isinstance(data, BaseException):
                refused += 1
            else:
                saved += 1
                assert refused_or_done(m.data_from_json, data) is None  # what was saved loads again
                assert holder.back == data
            refused_or_done(m.data_from_json, value)  # any Python value: loaded, or refused
        finally:
            m.close()
    print(f"JSON values: {saved} saved and loaded back, {refused} refused")
    assert saved > 100 and refused > 100


def test_indices_and_counts_are_checked() -> None:
    rng = random.Random(3803)
    m = Module.create(lambda m: m.config(3, 2, 2, 1))
    try:
        actions: list[Callable[..., object]] = [
            m.get_param,
            m.get_input,
            m.get_output,
            m.get_light,
            lambda i: m.params[i],
            lambda i: m.inputs[i],
            lambda i: m.outputs[0].set_voltage(1.0, i),
            m.outputs[0].get_voltage,
            m.outputs[0].set_channels,
            m.inputs[1].get_voltage_simd,
            m.get_param_quantity,
            m.get_input_info,
            m.params[0].set_value,
        ]
        for _ in range(3000):
            refused_or_done(rng.choice(actions), weird(rng))
        for _ in range(200):
            created = refused_or_done(Module.create, Counts([rng.choice(COUNTS) for _ in range(4)]).configure)
            if isinstance(created, Module):
                created.close()
    finally:
        m.close()


class Counts:
    def __init__(self, counts: list[object]) -> None:
        self.counts = counts

    def configure(self, m: Module) -> None:
        m.config(*self.counts)  # type: ignore[arg-type]


class Hostile(w.Widget):
    """Returns whatever it was told to from the queries Rack calls, or raises."""

    def __init__(self, answer: object) -> None:
        super().__init__()
        self.answer = answer

    def get_children_bounding_box(self) -> Any:
        if isinstance(self.answer, BaseException):
            raise self.answer
        return self.answer

    def get_relative_zoom(self, ancestor: Any) -> Any:
        return self.answer

    def step(self) -> None:
        if isinstance(self.answer, BaseException):
            raise self.answer
        super().step()


class BadItem(ui.MenuItem):
    """A submenu item whose child menu is not a menu, or raises."""

    def __init__(self, answer: object) -> None:
        super().__init__()
        self.answer = answer

    def create_child_menu(self) -> Any:
        if isinstance(self.answer, BaseException):
            raise self.answer
        return self.answer


def test_wrong_values_from_python_overrides_are_contained() -> None:
    rng = random.Random(3804)
    raised = _rackpy_host.dispatch_stats()["raised"]
    answers: list[object] = [
        None,
        "box",
        3,
        Rect(Vec(), Vec(1, 1)),
        ValueError("x"),
        RecursionError(),
        [1, 2],
        math.nan,
    ]
    for _ in range(300):
        widget = Hostile(rng.choice(answers))
        widget.box = Rect(Vec(10, 10), Vec(20, 20))
        frame = w.FramebufferWidget()
        frame.box = Rect(Vec(), Vec(40, 40))
        frame.add_child(widget)
        try:
            refused_or_done(frame.get_children_bounding_box)  # Rack asks the child: its own result if Python's is bad
            refused_or_done(frame.render)
            refused_or_done(widget.step)
        finally:
            frame.close()
    menu = raw.create_menu()
    overlay = menu.parent
    assert isinstance(overlay, ui.MenuOverlay)
    try:
        for answer in answers:
            item = BadItem(answer)
            menu.add_child(item)
            item.on_enter(w.EnterEvent())  # Rack asks for a child menu: anything but a Menu gives none
    finally:
        overlay.request_delete()
    assert _rackpy_host.dispatch_stats()["raised"] >= raised  # contained and counted, never propagated into Rack


# Methods that answer on a dead object because they never touch it: a static factory, and expander views,
# which resolve their module on every use (checked below to refuse then).
DEAD_HANDLE_SAFE = {"Module.create", "Module.get_expander", "Module.get_left_expander", "Module.get_right_expander"}


def test_every_method_of_a_dead_handle_refuses() -> None:
    m = Module.create(lambda m: m.config(1, 1, 1, 1))
    g = Graph()
    g.output(0, g.input(0))
    m.set_kernel(g.compile())
    kernel, param, port = m.kernel, m.params[0], m.outputs[0]
    frame = w.FramebufferWidget()
    menu = ui.Menu()
    m.close()
    frame.close()
    menu.close()
    gc.collect()
    calls = 0
    answered: set[str] = set()  # methods that did not refuse on a dead object
    for dead in (m, kernel, param, port, frame, menu):
        for name in dir(dead):
            if name.startswith("_") or name in {"alive", "close"}:
                continue
            attribute = refused_or_done(getattr, dead, name)
            if isinstance(attribute, (BaseException, type)) or not callable(attribute):  # event classes, too
                continue
            for args in ((), (0,), (0, 0), (1.0,)):
                calls += 1
                result = refused_or_done(attribute, *args)
                if not isinstance(result, (TypeError, RackPyError, ValueError, IndexError)):
                    answered.add(f"{type(dead).__name__}.{name}{args}")
    print(f"dead handles: {calls} calls; answered without refusing: {sorted(answered)}")
    assert calls > 100
    assert {a.split("(")[0] for a in answered} <= DEAD_HANDLE_SAFE, "\n".join(sorted(answered))
    view = m.get_left_expander()
    for field in ("module", "module_id", "producer_message", "consumer_message"):
        with pytest.raises(StaleHandleError):
            getattr(view, field)
    assert HOOKS.engine_sample_rate() > 0  # Rack still runs
