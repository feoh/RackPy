# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22C: laboratory quantities and the laboratory MIDI output device, inside Rack."""

import gc
import math
from typing import Any

import _rackpy_host
import pytest

from rackpy import laboratory, raw
from rackpy.errors import LaboratoryError, OwnershipError
from rackpy.laboratory import Bench, MidiOutputDevice, ModuleClass, ParamQuantity, Quantity, SwitchQuantity
from rackpy.raw.engine import Module
from rackpy.raw.math import clamp_safe
from rackpy.types import ValueQuantity

LIVE = _rackpy_host.bound_context_generation() is not None
HOOKS = _rackpy_host.TEST_HOOKS and LIVE


class PythonValue(Quantity):
    """A Python quantity with ValueQuantity's semantics, written in Python, counting Rack's calls."""

    def __init__(self, minimum: float, maximum: float, default: float) -> None:
        super().__init__()
        self.bounds = (minimum, maximum, default)
        self.value = clamp_safe(default, minimum, maximum)
        self.calls: dict[str, int] = {}

    def count(self, name: str) -> None:
        self.calls[name] = self.calls.get(name, 0) + 1

    def get_value(self) -> float:
        self.count("get_value")
        return self.value

    def set_value(self, value: float) -> None:
        self.count("set_value")
        self.value = clamp_safe(value, self.get_min_value(), self.get_max_value())

    def get_min_value(self) -> float:
        return self.bounds[0]

    def get_max_value(self) -> float:
        return self.bounds[1]

    def get_default_value(self) -> float:
        return self.bounds[2]


def test_laboratory_quantities_need_the_laboratory() -> None:
    laboratory.disable()
    for cls in (Quantity, ParamQuantity, SwitchQuantity, MidiOutputDevice):
        with pytest.raises(LaboratoryError, match=r"rackpy\.laboratory\.enable\(\)"):
            cls()
    laboratory.enable()
    q = ParamQuantity()
    assert _rackpy_host.laboratory.is_laboratory_quantity(q)
    assert not _rackpy_host.laboratory.is_laboratory_quantity(raw.engine.ParamQuantity())
    with pytest.raises(TypeError, match="Rack can call these on its engine thread"):
        type("Ordinary", (raw.engine.ParamQuantity,), {"get_value": lambda self: 1.0})


# (operation, arguments): Rack's Quantity methods that call the value and range methods.
OPERATIONS: list[tuple[str, tuple[Any, ...]]] = [
    ("set_value", (0.3,)),
    ("get_display_value", ()),
    ("set_display_value", (7.5,)),
    ("get_scaled_value", ()),
    ("set_scaled_value", (0.25,)),
    ("move_value", (100.0,)),
    ("is_max", ()),
    ("set_min", ()),
    ("is_min", ()),
    ("toggle", ()),
    ("get_range", ()),
    ("is_bounded", ()),
    ("move_scaled_value", (-0.5,)),
    ("to_scaled", (2.0,)),
    ("from_scaled", (0.75,)),
    ("set_display_value_string", ("3.25",)),
    ("get_display_value_string", ()),
    ("get_string", ()),
    ("set_value", (math.nan,)),
    ("reset", ()),
    ("set_max", ()),
]


def test_racks_quantity_code_reaches_python_value_methods_as_native_ones() -> None:
    python, native = PythonValue(-2, 10, 1), ValueQuantity(-2, 10, 1)
    for name, args in OPERATIONS:
        results = [getattr(q, name)(*args) for q in (python, native)]
        assert repr(results[0]) == repr(results[1]), name
        assert python.get_value() == native.get_value(), name
    # Rack's code really called the Python overrides: set_min, toggle, reset, ... all go through set_value.
    assert python.calls["set_value"] >= 10 and python.calls["get_value"] >= 15


def test_override_errors_reach_the_python_caller_through_racks_code() -> None:
    class Broken(Quantity):
        def get_value(self) -> float:
            raise LookupError("no value")

    with pytest.raises(LookupError, match="no value"):
        Broken().get_display_value_string()  # Rack's Quantity::getDisplayValueString calls getValue


def test_a_laboratory_param_quantity_drives_racks_parameter_code() -> None:
    class Scaled(ParamQuantity):
        def get_value(self) -> float:
            return 2.5

    q = Scaled()
    q.display_multiplier, q.display_offset = 4.0, 1.0
    assert q.get_display_value() == 11.0  # Rack's ParamQuantity::getDisplayValue, from Python's value
    assert q.get_display_value_string() == "11"

    class Labelled(SwitchQuantity):
        def get_value(self) -> float:
            return 1.0

    s = Labelled()
    s.min_value, s.max_value, s.labels = 0, 2, ["Off", "Half", "On"]
    assert s.get_display_value_string() == "Half"


def test_only_laboratory_modules_take_laboratory_parameter_quantities() -> None:
    class Knob(ParamQuantity):
        def get_value(self) -> float:
            return 3.0

    def configure(m: Module) -> None:
        m.config(1, 0, 0, 0)
        m.config_param(0, 0, 10, 5, "Knob", quantity_type=Knob)

    with pytest.raises(LaboratoryError, match="only into a laboratory module"):
        Module.create(configure)

    def raw_edit(m: Module) -> None:
        m.config(1, 0, 0, 0)
        m.param_quantities[0] = Knob()

    with pytest.raises(LaboratoryError, match="only into a laboratory module"):
        Module.create(raw_edit)

    class Lab(ModuleClass):
        def configure(self) -> None:
            configure(self.module)

    lab = Lab.create()
    knob = lab.module.param_quantities[0]
    assert isinstance(knob, Knob) and knob.get_value() == 3.0
    assert knob.module is lab.module


@pytest.mark.skipif(not LIVE, reason="needs Rack's live context: parameter quantities use Rack's engine")
def test_racks_module_code_reaches_slot_quantities_python_value_methods() -> None:
    class Knob(ParamQuantity):
        def get_default_value(self) -> float:
            return 4.0

        def get_max_value(self) -> float:
            return 6.0

        def get_value(self) -> float:
            return 2.0 * super().get_value()

    class Lab(ModuleClass):
        def configure(self) -> None:
            self.module.config(1, 0, 0, 0)
            self.module.config_param(0, 0, 10, 1, "Knob", quantity_type=Knob)

    lab = Lab.create()
    bench = Bench()
    bench.add(lab.module)
    bench.reset_module(lab.module)  # Rack's onReset -> ParamQuantity::reset -> setImmediateValue(getDefaultValue())
    assert lab.module.params[0].value == 4.0
    lab.module.params[0].value = 9.0
    assert lab.module.params_to_json() == [{"value": 18.0, "id": 0}]  # Rack's paramsToJson -> Python getValue
    lab.module.params_from_json([{"value": 8.0, "id": 0}])  # Rack's setImmediateValue clamps to Python's maximum
    assert lab.module.params[0].value == 6.0
    bench.remove(lab.module)


@pytest.mark.skipif(not HOOKS, reason="needs the test hooks and Rack's live context")
def test_rack_threads_never_run_laboratory_python() -> None:
    q = PythonValue(0, 1, 0.5)
    refused = _rackpy_host.dispatch_stats()["refused_rack_thread"]
    _rackpy_host._testing.read_quantity_on_rack_thread(q, 3)
    assert "get_value" not in q.calls  # Rack's own (empty) Quantity answered instead
    assert _rackpy_host.dispatch_stats()["refused_rack_thread"] > refused


@pytest.mark.skipif(not LIVE, reason="needs Rack's live context: MIDI ports")
def test_racks_output_reaches_a_python_output_device() -> None:
    got: list[list[int]] = []

    class Recorder(MidiOutputDevice):
        def send_message(self, message: raw.midi.Message) -> None:
            got.append(message.bytes)
            if message.get_note() == 0:
                raise ValueError("note zero")

    device = Recorder()
    out = raw.midi.Output()
    out.channel = 3
    device.connect(out)
    with pytest.raises(OwnershipError, match="already connected"):
        device.connect(out)
    assert device.connected == 1 and isinstance(out.output_device, raw.midi.OutputDevice)
    note = raw.midi.Message()
    note.bytes = [0x90, 60, 100]
    out.send_message(note)  # Rack's Output::sendMessage applies the port's channel, then calls the device
    assert got == [[0x93, 60, 100]]
    note.bytes = [0x90, 0, 100]
    with pytest.raises(ValueError, match="note zero"):
        out.send_message(note)

    class Sender(ModuleClass):
        def configure(self) -> None:
            self.module.config(0, 0, 0, 0)

        def process(self, args: laboratory.ProcessArgs) -> None:
            message = raw.midi.Message()
            message.bytes = [0xB0, 7, args.frame]
            out.send_message(message)  # synchronous, from a laboratory process()

    bench = Bench()
    bench.add(Sender.create().module)
    bench.step(3)
    assert got[-3:] == [[0xB3, 7, 0], [0xB3, 7, 1], [0xB3, 7, 2]]
    bench.clear()

    del device
    gc.collect()
    assert [out.output_device] == [None]
    out.send_message(note)  # no device: Rack sends nothing


@pytest.mark.skipif(not LIVE, reason="needs Rack's live context: MIDI ports")
def test_laboratory_midi_device_native_reentry_super_and_recursion_limit() -> None:
    calls = 0
    recursive = False
    out = raw.midi.Output()

    class Reentrant(MidiOutputDevice):
        def send_message(self, message: raw.midi.Message) -> None:
            nonlocal calls
            calls += 1
            super().send_message(message)  # OutputDevice's no-op, exactly once
            if recursive or calls == 1:
                out.send_message(message)

    device = Reentrant()
    device.connect(out)
    message = raw.midi.Message()
    message.bytes = [0x90, 60, 100]
    out.send_message(message)
    assert calls == 2
    calls, recursive = 0, True
    with pytest.raises(RecursionError, match="overrides nested more than 64"):
        out.send_message(message)
    assert calls == 64
    recursive = False
    out.send_message(message)
    assert calls == 65
