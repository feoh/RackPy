# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22B: a Bench dispatches module events as Rack's own engine does (live UI suite only).

The same edits (add, master, cables, bypass, reset, randomize, save, sample
rate, side-by-side placement, removal) are made to RackPy modules in Rack's
engine, through the test hooks and Rack's Engine API, and on a laboratory
Bench. Each module's events, as RackPy's native snapshot records them, must
be the same, in the same order; and a laboratory module that overrides every
event and calls super() must see exactly those events in Python.
"""

from collections.abc import Callable
from typing import Any

from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.laboratory import Bench, ModuleClass
from rackpy.raw.engine import Engine, Module

EVENTS = [
    "on_add",
    "on_remove",
    "on_bypass",
    "on_un_bypass",
    "on_port_change",
    "on_reset",
    "on_randomize",
    "on_save",
    "on_sample_rate_change",
    "on_set_master",
    "on_unset_master",
    "on_expander_change",
]


def engine() -> Engine:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    return context.engine


def configure(m: Module) -> None:
    m.config(1, 1, 1, 0)
    m.config_param(0, 0, 10, 2, "Level")


def events(m: Module) -> list[str]:
    return [repr(event) for _, event in m.lifecycle_events()]


def in_racks_engine() -> dict[str, list[str]]:
    e = engine()
    rate = e.get_sample_rate()
    previous = e.get_master_module()
    a, b = Module.create(configure), Module.create(configure)
    HOOKS.engine_adopt(a)
    HOOKS.engine_adopt(b)
    result: dict[str, list[str]] = {}
    e.set_master_module(a)  # Rack's own thread stops stepping
    cable = HOOKS.engine_connect(a, 0, b, 0)
    HOOKS.engine_bypass(b, True)
    HOOKS.engine_bypass(b, False)
    HOOKS.engine_reset(a)
    HOOKS.engine_randomize(b)
    HOOKS.engine_prepare_save()
    HOOKS.engine_set_sample_rate(96000)
    result["rates"] = [repr(e.get_sample_rate())]
    # Rack's patch loading sets the expander IDs (as its rack does for adjacent widgets); stepping follows them.
    for m, key, other in ((a, "rightModuleId", b), (b, "leftModuleId", a)):
        data = e.module_to_json(m)
        data[key] = other.id
        e.module_from_json(m, data)
    e.step_block(1)
    e.set_master_module(previous)
    HOOKS.engine_disconnect(cable)
    HOOKS.engine_set_sample_rate(rate)
    HOOKS.engine_delete(b, lambda: result.__setitem__("b", events(b)))
    HOOKS.engine_delete(a, lambda: result.__setitem__("a", events(a)))
    return result


def on_a_bench(make: Callable[[], Module], rates: list[str]) -> dict[str, list[str]]:
    e = engine()
    rate = e.get_sample_rate()
    bench = Bench(rate)
    a, b = make(), make()
    result: dict[str, list[str]] = {}
    bench.add(a)
    bench.add(b)
    bench.set_master_module(a)
    cable = bench.connect(a, 0, b, 0)
    bench.set_bypassed(b, True)
    bench.set_bypassed(b, False)
    bench.reset_module(a)
    bench.randomize_module(b)
    bench.prepare_save()
    bench.set_sample_rate(float(rates[0]))  # what Rack's settings let the engine use
    bench.set_expander(a, 1, b)
    bench.set_expander(b, 0, a)
    bench.step()
    bench.set_master_module(None)
    bench.disconnect(cable)
    bench.set_sample_rate(rate)
    bench.remove(b)
    result["b"] = events(b)
    bench.remove(a)
    result["a"] = events(a)
    return result


def test_bench_events_match_racks_engine() -> None:
    reference = in_racks_engine()
    rates = reference.pop("rates")
    assert reference == on_a_bench(lambda: Module.create(configure), rates)
    # Every kind of event the bench dispatches was exercised.
    seen = {event.split("(")[0] for sequence in reference.values() for event in sequence}
    assert seen >= {
        "Module.AddEvent",
        "Module.RemoveEvent",
        "Module.BypassEvent",
        "Module.UnBypassEvent",
        "Module.PortChangeEvent",
        "Module.ResetEvent",
        "Module.RandomizeEvent",
        "Module.SaveEvent",
        "Module.SampleRateChangeEvent",
        "Module.SetMasterEvent",
        "Module.UnsetMasterEvent",
        "Module.ExpanderChangeEvent",
    }

    log: dict[int, list[str]] = {}

    def override(name: str) -> Any:
        def method(self: ModuleClass, event: Any) -> None:
            log.setdefault(id(self.module), []).append(repr(event))
            getattr(ModuleClass, name)(self, event)  # Rack's implementation: the snapshot sees it too

        return method

    def lab_configure(self: ModuleClass) -> None:
        configure(self.module)

    recorder: type[ModuleClass] = type(
        "Recorder", (ModuleClass,), {"configure": lab_configure, **{n: override(n) for n in EVENTS}}
    )

    modules: list[Module] = []

    def make() -> Module:
        m = recorder.create().module
        modules.append(m)
        return m

    assert on_a_bench(make, rates) == reference
    assert [log[id(m)] for m in modules] == [reference["a"], reference["b"]]
