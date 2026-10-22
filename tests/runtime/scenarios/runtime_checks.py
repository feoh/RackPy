# SPDX-License-Identifier: GPL-3.0-or-later
"""Trusted entry module for tests/runtime: drives the runtime inside windowed Rack, one step per UI frame.

The RuntimeProbe widget calls on_step() on Rack's main thread every frame. The
scenario is a generator; each `yield` waits one frame. Every check logs
`CHECK <name> PASS|FAIL <detail>`; the test reads them from Rack's log.
"""

import functools
import gc
import threading
import time
import weakref
from collections.abc import Callable, Iterator
from pathlib import Path
from typing import Any

import _rackpy_host as host
from _rackpy_host import _testing as t

from rackpy import errors, runtime


class State:
    handle: Any = None
    frame = 0
    raised_once = False


state = State()


def check(name: str, ok: bool, detail: object = "") -> None:
    host.log_info(f"CHECK {name} {'PASS' if ok else 'FAIL'} {detail}")


def expect_raises(name: str, error: type[BaseException], call: Callable[[], object]) -> BaseException | None:
    try:
        call()
    except error as e:
        check(name, True, f"{type(e).__name__}: {e}")
        return e
    except Exception as e:
        check(name, False, f"raised {type(e).__name__}: {e}")
        return None
    check(name, False, "did not raise")
    return None


def wait(frames: int) -> Iterator[None]:
    for _ in range(frames):
        yield


def rss_kib() -> int:
    with Path("/proc/self/status").open() as f:
        return next(int(line.split()[1]) for line in f if line.startswith("VmRSS:"))


class Marker:
    pass


class Child(t.ProbeChild):
    removed = 0

    def on_removed(self) -> None:
        Child.removed += 1


def scenario() -> Iterator[None]:
    yield from wait(5)
    h = state.handle
    check("thread role on the UI thread", runtime.thread_role() == "main" and runtime.is_main_thread())
    check("context bound", host.bound_context_generation() is not None)

    # Identity: the same Rack object gives the same Python object.
    yield
    check("identity across frames", state.handle is h, f"{state.handle!r} vs {h!r}")
    p0 = h.processed
    yield from wait(5)
    check("audio keeps processing", h.processed > p0, f"{p0} -> {h.processed}")

    # A deleted module's handle is stale, not dangling.
    added = t.add_module()
    check("added module is alive", added.alive and not added.has_widget)
    yield from wait(3)
    check("added module processes", added.processed > 0)
    t.remove_module(added)
    check("deleted module handle is not alive", not added.alive)
    e = expect_raises(
        "stale handle raises StaleHandleError", errors.StaleHandleError, functools.partial(getattr, added, "processed")
    )
    check("StaleHandleError is a ReferenceError", isinstance(e, ReferenceError))
    del added

    # 1,000 create/delete cycles: no growth in wrappers, native references, objects or memory.
    gc.collect()
    entries0, refs0, objects0, rss0 = (
        host.identity_entries(),
        host.native_references(),
        len(gc.get_objects()),
        rss_kib(),
    )
    stale = 0
    for _ in range(10):
        for _ in range(100):
            m = t.add_module()
            t.remove_module(m)
            try:
                m.processed  # noqa: B018 - the access is the test
            except errors.StaleHandleError:
                stale += 1
        yield
    del m
    gc.collect()
    entries1, refs1, objects1, rss1 = (
        host.identity_entries(),
        host.native_references(),
        len(gc.get_objects()),
        rss_kib(),
    )
    check("1000 cycles: every deleted handle is stale", stale == 1000, stale)
    check("1000 cycles: identity registry does not grow", entries1 <= entries0, f"{entries0} -> {entries1}")
    check("1000 cycles: native references do not grow", refs1 == refs0, f"{refs0} -> {refs1}")
    check("1000 cycles: Python objects do not grow", objects1 - objects0 < 200, f"{objects0} -> {objects1}")
    check("1000 cycles: RSS growth", True, f"{rss0} -> {rss1} KiB")
    check("1000 cycles: RSS grows less than 4 MiB", rss1 - rss0 < 4096, f"{rss1 - rss0} KiB")

    # Wrong thread: UI-only operations refuse other threads; engine reads release the GIL and work.
    outcome: dict[str, object] = {}

    def worker() -> None:
        try:
            t.add_module()
            outcome["add"] = "no error"
        except errors.ThreadError as e:
            outcome["add"] = e
        outcome["count"] = t.engine_module_count()
        outcome["role"] = runtime.thread_role()

    thread = threading.Thread(target=worker)
    thread.start()
    thread.join()
    check(
        "UI-only call from a Python thread raises ThreadError",
        isinstance(outcome["add"], errors.ThreadError),
        outcome["add"],
    )
    check(
        "engine call from a Python thread works",
        isinstance(outcome["count"], int) and outcome["count"] >= 1,
        outcome["count"],
    )
    check("Python thread role is other", outcome["role"] == "other")

    # Cross-context: a handle refuses to work while another Rack context is active.
    expect_raises(
        "handle under a foreign context raises ContextError",
        errors.ContextError,
        lambda: t.with_foreign_context(lambda: h.processed),
    )
    check("foreign context recorded", any(d.code == "foreign_context" for d in runtime.diagnostics()))

    # Ownership: Rack owns a transferred child; handing it over twice fails.
    child = Child()
    t.attach_child(h, child)
    check("first transfer succeeds", child.alive)
    expect_raises(
        "second transfer raises OwnershipError", errors.OwnershipError, functools.partial(t.attach_child, h, child)
    )
    del child

    # Engine lock: a locking call inside an exclusive-lock callback is refused, not deadlocked.
    seen: list[object] = []

    def on_reset(handle: Any) -> None:
        seen.append(handle is h)
        try:
            t.engine_module_count()
            seen.append("no error")
        except errors.EngineLockError as e:
            seen.append(e)

    t.set_reset_callback(on_reset)
    t.reset_module(h)
    check("reset callback got the same handle", seen[:1] == [True], seen)
    check(
        "engine call inside onReset raises EngineLockError",
        len(seen) == 2 and isinstance(seen[1], errors.EngineLockError),
        seen,
    )

    # No Python on engine threads: Rack's attempt is refused and recorded.
    before = host.dispatch_stats()["refused_rack_thread"]
    t.try_python_on_engine_thread(h)
    yield from wait(5)
    after = host.dispatch_stats()["refused_rack_thread"]
    check("Python refused on an engine thread", after == before + 1, f"{before} -> {after}")
    refused = [d for d in runtime.diagnostics() if d.code == "refused_rack_thread"]
    check("refusal recorded from a rack thread", bool(refused) and refused[-1].thread == "rack", refused[-1:])

    # A native holder dropping its Python reference on an engine thread: released later, on the UI thread.
    marker = Marker()
    alive = weakref.ref(marker)
    t.drop_on_engine_thread(h, marker)
    del marker
    check("object held by the native holder", alive() is not None)
    for _ in range(30):
        yield
        if alive() is None:
            break
    check(
        "object released on the UI thread after an engine-thread drop", alive() is None and host.pending_releases() == 0
    )

    # A callback that raises is caught at the Rack boundary.
    raised0 = host.dispatch_stats()["raised"]
    state.raised_once = True  # on_step raises once on the next frame
    yield from wait(3)
    check("raising callback was caught", host.dispatch_stats()["raised"] == raised0 + 1)
    check(
        "raising callback was recorded",
        any(d.code == "callback_raised" and "deliberate" in d.message for d in runtime.diagnostics()),
    )

    # Diagnostics: bounded, ordered, and readable incrementally.
    last = runtime.diagnostics()[-1].sequence
    for i in range(300):
        t.record_diagnostic(f"bulk {i}")
    newer = runtime.diagnostics(last)
    check("diagnostics are bounded", len(newer) == host.DIAGNOSTIC_CAPACITY, len(newer))
    check("diagnostics keep the newest", newer[-1].message == "bulk 299" and newer[-1].code == "test", newer[-1])

    # Shutdown: one worker is stopped by on_context_closing; one is left running on purpose.
    calls = {"stopped": 0, "unstopped": 0}
    stop = threading.Event()

    def polite() -> None:
        while not stop.is_set():
            t.engine_module_count()
            calls["stopped"] += 1
            time.sleep(0.001)

    def careless() -> None:
        while True:
            try:
                h.processed  # noqa: B018 - the access is the test
                t.engine_module_count()
                calls["unstopped"] += 1
            except errors.StaleHandleError as e:
                host.log_info(f"WORKER careless saw {type(e).__name__} after {calls['unstopped']} calls: {e}")
                return
            time.sleep(0.001)

    workers = [threading.Thread(target=polite), threading.Thread(target=careless, daemon=True)]
    for w in workers:
        w.start()

    @runtime.on_context_closing
    def stop_polite() -> None:
        stop.set()
        workers[0].join()
        host.log_info(f"WORKER polite joined after {calls['stopped']} calls")

    @runtime.on_context_closing
    def failing_hook() -> None:
        raise RuntimeError("deliberate failure in a closing hook")

    yield from wait(10)
    check("workers ran", calls["stopped"] > 0 and calls["unstopped"] > 0, calls)
    host.log_info("SCENARIO DONE")
    t.close_window()


steps = scenario()


def on_step(frame: int, handle: Any) -> None:
    state.handle = handle
    state.frame = frame
    if state.raised_once:
        state.raised_once = False
        raise RuntimeError("deliberate failure in a step callback")
    next(steps, None)


t.set_step_callback(on_step)
