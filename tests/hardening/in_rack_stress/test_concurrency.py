# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38: the main thread changes everything while Rack's engine thread runs the modules.

A seeded random mix of operations, each one Rack's own path where the test hooks
have it: adding and deleting modules, cabling, parameters, kernel controls and whole
kernels, sample-rate changes, reset, randomize, bypass, saves, state replacement,
menus opened and closed, and expander neighbours set and then deleted under the
module reading them. The run is bounded by the outer test's timeout, so a deadlock
fails it. Then Python's callbacks turn slow, then raise, and Rack's engine must keep
running at full rate meanwhile: RackPy holds none of Rack's engine locks while
Python runs, and Python never runs on the engine thread.
"""

import random
import time
from typing import Any

import _rackpy_host
import stress_models as sm
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.dsp.kernel import load
from rackpy.engine import Module
from rackpy.raw import ui
from rackpy.runtime import diagnostics

OPERATIONS = 4000
RATES = [44100.0, 48000.0, 96000.0, 22050.0]


def engine_frame() -> int:
    context = raw.APP()
    assert context is not None and context.engine is not None
    return context.engine.get_frame()


def with_data(module: Module, data: object) -> dict[str, Any]:
    """The module's own JSON (Rack's fromJson needs its plugin and model), with other custom data."""
    return {**HOOKS.engine_module_to_json(module), "data": data}


class Patch:
    """The modules and engine cables the run made, so that each operation picks live ones."""

    def __init__(self, rng: random.Random) -> None:
        self.rng = rng
        self.modules: dict[int, Module] = {}
        self.cables: dict[int, tuple[int, int]] = {}  # cable ID: (output module, input module)
        self.counts: dict[str, int] = {}

    def add(self) -> None:
        module_id = HOOKS.browser_add_module("RackPy", "Churn")
        module = HOOKS.engine_handle(module_id)
        assert module is not None
        self.modules[module_id] = module

    def pick(self) -> tuple[int, Module]:
        module_id = self.rng.choice(list(self.modules))
        return module_id, self.modules[module_id]

    def delete(self, module_id: int) -> None:
        for cable, ends in list(self.cables.items()):
            if module_id in ends:
                HOOKS.engine_disconnect(cable)
                del self.cables[cable]
        HOOKS.remove_module_widget(module_id)
        del self.modules[module_id]

    def run(self, operation: str) -> None:
        self.counts[operation] = self.counts.get(operation, 0) + 1
        getattr(self, "op_" + operation)()

    def op_add(self) -> None:
        if len(self.modules) < 24:
            self.add()

    def op_delete(self) -> None:
        if len(self.modules) > 2:
            self.delete(self.pick()[0])

    def op_connect(self) -> None:
        (a, ma), (b, mb) = self.pick(), self.pick()
        port = self.rng.randrange(2)
        if any(ends[1] == b and port == p for ends, p in ((e, 0) for e in self.cables.values())):
            return
        try:
            cable = HOOKS.engine_connect(ma, self.rng.randrange(3), mb, port)
        except ValueError, RuntimeError:
            return  # Rack refuses a second cable into one input
        self.cables[cable] = (a, b)

    def op_disconnect(self) -> None:
        if self.cables:
            cable = self.rng.choice(list(self.cables))
            HOOKS.engine_disconnect(cable)
            del self.cables[cable]

    def op_param(self) -> None:
        self.pick()[1].params[0].value = self.rng.uniform(-2, 2)

    def op_control(self) -> None:
        sm.Churn.of(self.pick()[1]).kernel.set("amp", self.rng.uniform(0, 10))

    def op_kernel(self) -> None:
        churn = sm.Churn.of(self.pick()[1])
        churn.kernel = sm.GRAPH.compile()
        churn.module.set_kernel(churn.kernel)  # replaced while the engine thread runs the old one

    def op_rate(self) -> None:
        HOOKS.engine_set_sample_rate(self.rng.choice(RATES))

    def op_reset(self) -> None:
        HOOKS.engine_reset(self.pick()[1])

    def op_randomize(self) -> None:
        HOOKS.engine_randomize(self.pick()[1])

    def op_bypass(self) -> None:
        HOOKS.engine_bypass(self.pick()[1], self.rng.random() < 0.5)

    def op_save(self) -> None:
        HOOKS.engine_prepare_save()
        HOOKS.engine_module_to_json(self.pick()[1])

    def op_state(self) -> None:
        state = {"steps": [self.rng.uniform(-1, 1) for _ in range(sm.STEPS)], "label": "replaced"}
        module = self.pick()[1]
        HOOKS.engine_module_from_json(module, with_data(module, state))

    def op_menu(self) -> None:
        menu = raw.create_menu()
        for i in range(self.rng.randrange(1, 6)):
            menu.add_child(raw.create_menu_label(f"item {i}"))
        overlay = menu.parent
        assert isinstance(overlay, ui.MenuOverlay)
        overlay.request_delete()  # closed again before Rack's next frame draws it

    def op_expander(self) -> None:
        if len(self.modules) < 4:
            return
        (a, ma), (b, mb) = self.pick(), self.pick()
        if a == b:
            return
        HOOKS.rack_set_expander_id(ma, 1, b)
        HOOKS.rack_set_expander_id(mb, 0, a)
        time.sleep(0.002)  # the engine thread exchanges lanes for a few blocks
        self.delete(b)  # hot removal: Rack clears a's right expander as it removes b


def test_engine_and_main_thread_race() -> None:
    rng = random.Random(3806)
    patch = Patch(rng)
    for _ in range(4):
        patch.add()
    operations = [name.removeprefix("op_") for name in dir(Patch) if name.startswith("op_")]
    raised = _rackpy_host.dispatch_stats()["raised"]
    for _ in range(OPERATIONS):
        patch.run(rng.choice(operations))
    print(f"operations: {dict(sorted(patch.counts.items()))}")
    assert set(patch.counts) == set(operations)
    assert _rackpy_host.dispatch_stats()["raised"] == raised  # no callback failed while nothing was hostile
    HOOKS.engine_set_sample_rate(48000.0)
    for module in patch.modules.values():
        HOOKS.engine_bypass(module, False)
    before = {i: m.outputs[sm.FRAMES].get_voltage() for i, m in patch.modules.items()}
    time.sleep(0.2)
    after = {i: m.outputs[sm.FRAMES].get_voltage() for i, m in patch.modules.items()}
    assert all(after[i] > before[i] for i in before), (before, after)  # every kernel still runs
    for module_id in list(patch.modules):
        patch.delete(module_id)


def frames_during(action: Any) -> tuple[float, float]:
    """Engine frames per second of wall time while ``action`` ran on the main thread, and the seconds it took."""
    t0, f0 = time.monotonic(), engine_frame()
    action()
    t1, f1 = time.monotonic(), engine_frame()
    return (f1 - f0) / (t1 - t0), t1 - t0


def repeat_for(action: Any, seconds: float) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        action()


def latest_sequence() -> int:
    recorded = diagnostics()
    return recorded[-1].sequence if recorded else 0


def test_slow_and_failing_python_never_stalls_the_engine() -> None:
    """Python outside Rack's exclusive engine lock never holds up the engine; inside it, it is diagnosed.

    Rack calls a module's fromJson, reset, randomize and bypass events with its engine
    lock held exclusively, and RackPy runs their Python handlers there, as Rack runs C++
    ones: audio waits for them, as ADR 004 measured. RackPy records each slow one. Every
    other callback (toJson, onSave, a widget's step) runs while the engine runs.
    """
    HOOKS.engine_set_sample_rate(48000.0)
    patch = Patch(random.Random(3807))
    for _ in range(6):
        patch.add()
    guard = HOOKS.audio_guard_report() is not None
    if guard:
        HOOKS.audio_guard_reset()
    modules = list(patch.modules.values())
    restless = sm.Restless()

    def outside() -> None:
        for module in modules:
            HOOKS.engine_module_to_json(module)
            HOOKS.call_virtual(restless, "step")
        HOOKS.engine_prepare_save()

    payloads = [with_data(module, {"bad": True}) for module in modules]  # toJson runs outside the lock: not here

    def inside() -> None:
        for module, payload in zip(modules, payloads, strict=True):
            HOOKS.engine_module_from_json(module, payload)
            HOOKS.engine_reset(module)
            HOOKS.engine_randomize(module)
            HOOKS.engine_bypass(module, True)
            HOOKS.engine_bypass(module, False)

    def both() -> None:
        outside()
        inside()

    idle_rate, _ = frames_during(lambda: time.sleep(0.5))  # the baseline: Rack's engine with nothing in its way
    try:
        sm.Hostility.slow = 0.02
        calls = sm.Hostility.calls
        slow_rate, slow_seconds = frames_during(outside)
        slow_calls = sm.Hostility.calls - calls
        mark, calls = latest_sequence(), sm.Hostility.calls
        exclusive_rate, _ = frames_during(inside)
        exclusive_calls = sm.Hostility.calls - calls
        slow_exclusive = [d for d in diagnostics(mark) if d.code == "slow_exclusive_callback"]
        sm.Hostility.slow, sm.Hostility.fail = 0.0, True
        raised = _rackpy_host.dispatch_stats()["raised"]
        failing_rate, failing_seconds = frames_during(lambda: repeat_for(both, 0.5))
        failed = _rackpy_host.dispatch_stats()["raised"] - raised
    finally:
        sm.Hostility.slow, sm.Hostility.fail = 0.0, False
        restless.close()
    print(
        f"engine frames/s: idle {idle_rate:.0f}; while {slow_calls} Python callbacks outside the exclusive lock "
        f"slept 20 ms each ({slow_seconds:.2f} s) {slow_rate:.0f}; while {exclusive_calls} inside it did "
        f"{exclusive_rate:.0f}, with {len(slow_exclusive)} slow_exclusive_callback diagnostics; "
        f"while {failed} raised ({failing_seconds:.2f} s) {failing_rate:.0f}"
    )
    assert slow_calls >= 2 * len(modules) and slow_seconds >= 0.02 * slow_calls * 0.9
    assert slow_rate > idle_rate * 0.9, (idle_rate, slow_rate)  # nothing outside the lock waited on Python
    assert exclusive_calls >= 5 * len(modules) and len(slow_exclusive) >= exclusive_calls, slow_exclusive[-3:]
    assert failed >= 7 * len(modules)  # contained: Rack's paths went on, and RackPy counted each
    assert failing_rate > idle_rate * 0.9, (idle_rate, failing_rate)
    if guard:
        report = HOOKS.audio_guard_report()
        print(f"audio guard inside RackPy's process(): {report}")
        assert report is not None and not any(report.values()), report
    for module_id in list(patch.modules):
        patch.delete(module_id)


def test_a_callback_raising_every_frame_is_logged_boundedly() -> None:
    """5,000 raises from a C++ virtual's Python override: all counted; the outer test counts the log's lines."""
    widget = sm.Restless()
    raised = _rackpy_host.dispatch_stats()["raised"]
    sm.Hostility.fail = True
    try:
        for _ in range(5000):
            HOOKS.call_virtual(widget, "step")
    finally:
        sm.Hostility.fail = False
        widget.close()
    assert _rackpy_host.dispatch_stats()["raised"] - raised == 5000
    print("raised 5000 times from Widget::step")


def test_the_audio_guard_sees_inside_process_in_live_rack() -> None:
    """The guard's positive control: a kernel that allocates every frame is counted (only when preloaded)."""
    if HOOKS.audio_guard_report() is None:
        return
    module = Module.create(lambda m: m.config(0, 0, 1, 0))
    module.set_kernel(load("rackpy_test.Allocate")())
    module_id = HOOKS.engine_adopt(module)
    try:
        HOOKS.audio_guard_reset()
        time.sleep(0.1)
        report = HOOKS.audio_guard_report()
        assert report is not None and report["malloc"] > 1000 and report["free"] > 1000, report
    finally:
        handle = HOOKS.engine_handle(module_id)
        assert handle is not None
        HOOKS.engine_delete(handle)
        HOOKS.audio_guard_reset()
