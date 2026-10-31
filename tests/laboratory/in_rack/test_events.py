# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22B: laboratory overrides of module events and JSON, dispatched synchronously by a Bench, inside Rack."""

from typing import Any

import _rackpy_host
import pytest

from rackpy import raw
from rackpy.errors import ContextError, LaboratoryError
from rackpy.laboratory import Bench, ModuleClass
from rackpy.raw.engine import Module

LIVE = _rackpy_host.bound_context_generation() is not None
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


def describe(event: object) -> str:
    """An event's type and fields, comparable across sources."""
    return repr(event)


def kinds(m: Module) -> list[str]:
    return [describe(event) for _, event in m.lifecycle_events()]


def recorder(log: list[tuple[str, str]], call_super: bool) -> type[ModuleClass]:
    """A laboratory class that logs every event override, then (optionally) runs Rack's implementation."""

    def override(name: str) -> Any:
        def method(self: ModuleClass, event: Any) -> None:
            log.append((f"{name}@{self.module.id}", describe(event)))
            if call_super:
                getattr(ModuleClass, name)(self, event)

        method.__name__ = name
        return method

    def configure(self: ModuleClass) -> None:
        self.module.config(0, 1, 1, 0)

    return type("Recorder", (ModuleClass,), {"configure": configure, **{name: override(name) for name in EVENTS}})


def test_expander_change_events_are_rack_values() -> None:
    e = Module.ExpanderChangeEvent()
    assert e.side == 0 and repr(e) == "Module.ExpanderChangeEvent(side=0)"
    e.side = 1
    assert repr(Module.ExpanderChangeEvent(1)) == repr(e)
    with pytest.raises(ValueError, match="side is 0"):
        Module.ExpanderChangeEvent(2)
    with pytest.raises(ValueError, match="side is 0"):
        e.side = -1


def test_the_bench_dispatches_events_in_racks_order() -> None:
    log: list[tuple[str, str]] = []
    cls = recorder(log, call_super=True)
    bench = Bench(48000)
    a, b = cls.create(), cls.create()
    bench.add(a.module)
    bench.add(b.module)
    cable = bench.connect(a.module, 0, b.module, 0)
    bench.set_bypassed(a.module, True)
    bench.set_bypassed(a.module, False)
    bench.set_sample_rate(96000)
    bench.set_master_module(a.module)
    bench.set_master_module(b.module)
    assert bench.master_module is b.module
    bench.reset_module(a.module)
    bench.randomize_module(b.module)
    bench.prepare_save()
    bench.prepare_save_module(b.module)
    bench.set_expander(a.module, 1, b.module)
    bench.set_expander(b.module, 0, a.module)
    assert bench.get_expander_module(a.module, 1) is None  # until the next step, as in Rack
    bench.step()
    assert bench.get_expander_module(a.module, 1) is b.module
    bench.step()  # no change, no event
    bench.disconnect(cable)
    bench.remove(b.module)  # the master: unset; a's right expander cleared
    assert [bench.master_module, bench.get_expander_module(a.module, 1)] == [None, None]
    srate = repr(Module.SampleRateChangeEvent(48000, 1 / 48000))
    fast = repr(Module.SampleRateChangeEvent(96000, 1 / 96000))
    port = "Module.PortChangeEvent(connecting={}, type={}, port_id={})"
    assert log == [
        ("on_add@0", "Module.AddEvent()"),
        ("on_sample_rate_change@0", srate),
        ("on_add@1", "Module.AddEvent()"),
        ("on_sample_rate_change@1", srate),
        ("on_port_change@1", port.format("True", "INPUT", 0)),
        ("on_port_change@0", port.format("True", "OUTPUT", 0)),
        ("on_bypass@0", "Module.BypassEvent()"),
        ("on_un_bypass@0", "Module.UnBypassEvent()"),
        ("on_sample_rate_change@0", fast),
        ("on_sample_rate_change@1", fast),
        ("on_set_master@0", "Module.SetMasterEvent()"),
        ("on_unset_master@0", "Module.UnsetMasterEvent()"),
        ("on_set_master@1", "Module.SetMasterEvent()"),
        ("on_reset@0", "Module.ResetEvent()"),
        ("on_randomize@1", "Module.RandomizeEvent()"),
        ("on_save@0", "Module.SaveEvent()"),
        ("on_save@1", "Module.SaveEvent()"),
        ("on_save@1", "Module.SaveEvent()"),
        ("on_expander_change@0", "Module.ExpanderChangeEvent(side=1)"),
        ("on_expander_change@1", "Module.ExpanderChangeEvent(side=0)"),
        ("on_port_change@1", port.format("False", "INPUT", 0)),
        ("on_port_change@0", port.format("False", "OUTPUT", 0)),
        ("on_remove@1", "Module.RemoveEvent()"),
        ("on_unset_master@1", "Module.UnsetMasterEvent()"),
        ("on_expander_change@0", "Module.ExpanderChangeEvent(side=1)"),
        ("on_expander_change@1", "Module.ExpanderChangeEvent(side=0)"),
    ]
    # super() ran the RackPy module's adapters, so the snapshots saw the same events.
    assert [d for n, d in log if n.endswith("@0")] == kinds(a.module)


def test_an_override_replaces_racks_behaviour_unless_it_calls_super() -> None:
    log: list[tuple[str, str]] = []
    silent, kept = recorder(log, call_super=False).create(), recorder(log, call_super=True).create()
    bench = Bench()
    bench.add(silent.module)
    bench.add(kept.module)
    assert kinds(silent.module) == []  # Rack's adapter (and its snapshot) did not run
    assert kinds(kept.module) == ["Module.AddEvent()", repr(Module.SampleRateChangeEvent(44100, 1 / 44100))]


def test_override_errors_reach_the_bench_caller_and_later_modules_still_get_events() -> None:
    class Fails(ModuleClass):
        def configure(self) -> None:
            self.module.config(0, 0, 0, 0)

        def on_sample_rate_change(self, event: Module.SampleRateChangeEvent) -> None:
            raise ArithmeticError(f"rate {event.sample_rate:g}")

    log: list[tuple[str, str]] = []
    bench = Bench()
    raised = _rackpy_host.dispatch_stats()["raised"]
    fails, after = Fails.create(), recorder(log, call_super=False).create()
    with pytest.raises(ArithmeticError, match="rate 44100"):
        bench.add(fails.module)
    assert bench.modules == (fails.module,)  # Rack adds it before dispatching
    bench.add(after.module)
    with pytest.raises(ArithmeticError, match="rate 96000"):
        bench.set_sample_rate(96000)
    assert log[-1] == ("on_sample_rate_change@1", repr(Module.SampleRateChangeEvent(96000, 1 / 96000)))
    assert _rackpy_host.dispatch_stats()["raised"] == raised  # raised to the caller, not contained


def test_json_overrides_answer_synchronously() -> None:
    seen: list[Any] = []

    class Stateful(ModuleClass):
        def configure(self) -> None:
            self.module.config(0, 0, 0, 0)
            self.level = 3

        def to_json(self) -> Any:
            return {"level": self.level}

        def from_json(self, data: Any) -> None:
            self.level = data["level"]

        def data_to_json(self) -> Any:
            data = super().data_to_json()  # the data handler above
            assert isinstance(data, dict)
            return {**data, "lab": True}

        def data_from_json(self, data: Any) -> None:
            seen.append(data)
            super().data_from_json(data)

        def module_to_json(self) -> Any:
            return {"whole": self.module.data_to_json()}

        def module_from_json(self, data: Any) -> None:
            self.module.data_from_json(data["whole"])

    s = Stateful.create()
    assert s.module.data_to_json() == {"level": 3, "lab": True}
    s.module.data_from_json({"level": 7, "lab": True})
    assert s.level == 7 and seen == [{"level": 7, "lab": True}]
    assert s.module.to_json() == {"whole": {"level": 7, "lab": True}}  # no model: the override does not need one
    s.module.from_json({"whole": {"level": 9}})
    assert s.level == 9

    class Plain(ModuleClass):
        def configure(self) -> None:
            self.module.config(0, 0, 0, 0)

    plain = Plain.create()
    assert plain.module.data_to_json() is None  # Rack's NULL: no data
    with pytest.raises(LaboratoryError, match="name the module's model"):
        plain.module.to_json()
    with pytest.raises(LaboratoryError, match="name the module's model"):
        plain.module.from_json({})
    with pytest.raises(LaboratoryError, match="name the module's model"):
        ModuleClass.module_to_json(plain)

    class Broken(Plain):
        def data_to_json(self) -> Any:
            return {"not json": object()}

    with pytest.raises(TypeError):
        Broken.create().module.data_to_json()


@pytest.mark.skipif(LIVE, reason="startup run only: no Rack context")
def test_parameter_events_and_json_need_racks_context() -> None:
    class Knob(ModuleClass):
        def configure(self) -> None:
            self.module.config(1, 0, 0, 0)

    knob = Knob.create()
    bench = Bench()
    bench.add(knob.module)
    with pytest.raises(ContextError, match="live bound UI context"):
        bench.reset_module(knob.module)
    with pytest.raises(ContextError, match="live bound UI context"):
        bench.randomize_module(knob.module)
    with pytest.raises(ContextError, match="live bound UI context"):
        ModuleClass.params_to_json(knob)


@pytest.mark.skipif(not LIVE, reason="needs Rack's live context: parameter quantities use Rack's engine")
def test_racks_to_json_calls_python_params_and_data_overrides() -> None:
    model = raw.plugin.get_model("RackPy", "RuntimeProbe")
    assert model is not None
    calls: list[str] = []

    class Saved(ModuleClass):
        def configure(self) -> None:
            self.module.config(2, 0, 0, 0)
            self.module.config_param(0, 0, 10, 2, "A")
            self.module.config_param(1, -1, 1, 0, "B")

        def params_to_json(self) -> Any:
            calls.append("params_to_json")
            params = super().params_to_json()  # Rack's: each quantity's value and ID
            assert isinstance(params, list)
            return [p for p in params if isinstance(p, dict) and p["id"] == 0]

        def params_from_json(self, data: Any) -> None:
            calls.append(f"params_from_json {data}")
            super().params_from_json(data)

        def data_to_json(self) -> Any:
            calls.append("data_to_json")
            return {"x": 1}

        def data_from_json(self, data: Any) -> None:
            calls.append(f"data_from_json {data}")

        def on_reset(self, event: Module.ResetEvent) -> None:
            calls.append("on_reset")  # no super(): Rack resets nothing

    saved = Saved.create(model)
    assert saved.module.model == model
    saved.module.params[0].value = 4
    saved.module.params[1].value = 0.5
    bench = Bench()
    bench.add(saved.module)
    bench.reset_module(saved.module)
    assert [p.value for p in saved.module.params] == [4, 0.5]
    j = saved.module.to_json()  # Rack's Module::toJson, which calls the overrides
    assert (j["plugin"], j["model"]) == ("RackPy", "RuntimeProbe")
    assert j["params"] == [{"value": 4.0, "id": 0}] and j["data"] == {"x": 1}
    j["params"] = [{"value": 6.0, "id": 0}, {"value": -0.25, "id": 1}]
    saved.module.from_json(j)  # Rack's Module::fromJson
    assert [p.value for p in saved.module.params] == [6, -0.25]
    assert calls == [
        "on_reset",
        "params_to_json",
        "data_to_json",
        "params_from_json [{'value': 6.0, 'id': 0}, {'value': -0.25, 'id': 1}]",
        "data_from_json {'x': 1}",
    ]
    bench.remove(saved.module)


def test_driver_and_engine_events_have_no_python_path_outside_the_laboratory() -> None:
    for name in ("on_sample_rate_change", "on_set_master", "on_unset_master", "on_expander_change"):

        def install(m: Module, name: str = name) -> None:
            m.set_event_handlers(**{name: print})

        with pytest.raises(TypeError, match="unexpected event"):
            Module.create(install)


def test_same_module_native_json_reentry_and_nonvirtual_super() -> None:
    seen: list[str] = []

    class Reentrant(ModuleClass):
        def configure(self) -> None:
            self.module.config(0, 0, 0, 0)

        def data_to_json(self) -> Any:
            seen.append("params")
            if len(seen) == 1:
                assert self.module.data_to_json() is None
            return super().data_to_json()

    instance = Reentrant.create()
    try:
        assert instance.module.data_to_json() is None
        assert seen == ["params", "params"]
    finally:
        instance.module.close()
