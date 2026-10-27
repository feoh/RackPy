# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22E: a Python override of dsp::MidiGenerator::onMessage, in the laboratory, inside Rack."""

import gc
from typing import Any

import _rackpy_host
import pytest

from rackpy import laboratory
from rackpy.errors import LaboratoryError
from rackpy.raw import dsp
from rackpy.raw.midi import Message


def drive(g: Any) -> None:
    """Gates, velocities, pressures, controllers, the pitch wheel, clock and transport, then a panic."""
    g.set_channels(3)
    for c, note in enumerate((60, 64, 67)):
        g.set_velocity(90 + c, c)
        g.set_note_gate(note, True, c)
    g.set_key_pressure(40, 1)
    g.set_channel_pressure(33)
    g.set_cc(12, 74)
    g.set_mod_wheel(5)
    g.set_pitch_wheel(1234)
    g.set_clock(True)
    g.set_start(True)
    g.set_note_gate(64, False, 1)
    g.set_frame(77)
    g.set_stop(True)
    g.panic()


def test_normal_mode_still_refuses_on_message_overrides() -> None:
    with pytest.raises(TypeError, match=r"rackpy\.laboratory\.MidiGenerator\[N\]"):
        type("Ordinary", (dsp.MidiGenerator[4],), {"on_message": lambda self, message: None})
    laboratory.disable()
    with pytest.raises(LaboratoryError, match="creating a laboratory MIDI generator"):
        laboratory.MidiGenerator[4]()
    with pytest.raises(TypeError, match="not defined in Rack"):
        laboratory.MidiGenerator[17]


@pytest.mark.parametrize("channels", [1, 4, 16])
def test_racks_generator_calls_the_python_override_with_what_it_records(channels: int) -> None:
    seen: list[tuple[list[int], int]] = []
    retained: list[Message] = []
    base: Any = laboratory.MidiGenerator[channels]

    class Watching(base):  # type: ignore[misc]
        def on_message(self, message: Message) -> None:
            seen.append((message.bytes, message.frame))
            retained.append(message)
            super().on_message(message)  # RackPy's native implementation: records it

    watching, native = Watching(), dsp.MidiGenerator[channels]()
    assert isinstance(watching, dsp.MidiGenerator[channels]) and laboratory.MidiGenerator[channels] is base
    assert not hasattr(native, "on_message")  # an ordinary generator records, and has no override point
    if channels >= 3:
        drive(watching)
        drive(native)
    else:
        for g in (watching, native):
            g.set_note_gate(60, True, 0)
            g.set_cc(1, 7)
            g.panic()
    expected = [(m.bytes, m.frame) for m in native.take_messages()]
    assert expected and seen == expected  # synchronous, in Rack's order, with Rack's bytes and frames
    assert [(m.bytes, m.frame) for m in watching.take_messages()] == expected
    del watching, native
    gc.collect()
    assert [(m.bytes, m.frame) for m in retained] == expected  # owned messages, not Rack's stack pointers


def test_an_override_replaces_the_recording_and_its_errors_reach_the_caller() -> None:
    base: Any = laboratory.MidiGenerator[2]

    class Silent(base):  # type: ignore[misc]
        def on_message(self, message: Message) -> None:
            if message.get_note() == 61:
                raise ValueError("note 61")

    g = Silent()
    g.set_note_gate(60, True, 0)
    assert g.take_messages() == []  # no super(): nothing recorded
    with pytest.raises(ValueError, match="note 61"):
        g.set_note_gate(61, True, 1)
    message = Message()
    message.bytes = [0x90, 1, 2]
    base.on_message(g, message)  # the superclass implementation, called directly: RackPy's records
    assert [m.bytes for m in g.take_messages()] == [[0x90, 1, 2]]


def test_a_bench_process_can_generate_midi_synchronously() -> None:
    base: Any = laboratory.MidiGenerator[1]
    seen: list[int] = []

    class Generator(base):  # type: ignore[misc]
        def on_message(self, message: Message) -> None:
            seen.append(message.frame)
            super().on_message(message)

    g = Generator()

    class Sender(laboratory.ModuleClass):
        def configure(self) -> None:
            self.module.config(0, 0, 0, 0)

        def process(self, args: laboratory.ProcessArgs) -> None:
            g.set_frame(args.frame)
            g.set_note_gate(60 + args.frame, True, 0)
            assert seen[-1] == args.frame

    bench = laboratory.Bench()
    bench.add(Sender.create().module)
    bench.step(3)
    assert set(seen) == {0, 1, 2}
    assert [m.frame for m in g.take_messages()] == seen
    bench.clear()


@pytest.mark.skipif(not _rackpy_host.TEST_HOOKS, reason="needs test hooks to call onMessage on a Rack thread")
def test_rack_threads_never_run_midi_generator_python() -> None:
    base: Any = laboratory.MidiGenerator[16]
    seen: list[Message] = []

    class Generator(base):  # type: ignore[misc]
        def on_message(self, message: Message) -> None:
            seen.append(message)
            raise AssertionError("Python must never run on Rack's engine/driver threads")

    g = Generator()
    message = Message()
    message.bytes, message.frame = [0x92, 60, 100], 123
    refused = _rackpy_host.dispatch_stats()["refused_rack_thread"]
    _rackpy_host._testing.midi_generator_16_on_rack_thread(g, message)
    assert seen == []
    assert _rackpy_host.dispatch_stats()["refused_rack_thread"] == refused + 1
    assert [(m.bytes, m.frame) for m in g.take_messages()] == [(message.bytes, message.frame)]


def test_inherited_callbacks_and_late_monkeypatches_do_not_bypass_enablement() -> None:
    laboratory.disable()
    seen: list[Message] = []

    def on_message(self: Any, message: Message) -> None:
        seen.append(message)

    mixin = type("Mixin", (), {"on_message": on_message})
    with pytest.raises(TypeError, match="defines on_message"):
        type("Inherited", (mixin, dsp.MidiGenerator[16]), {})

    # A Python alias is not enough: dispatch also needs the native laboratory mark.
    raw_class: Any = type("Ordinary", (dsp.MidiGenerator[16],), {})
    instance_patch = raw_class()
    instance_patch.on_message = seen.append
    instance_patch.set_note_gate(60, True, 0)
    assert [m.bytes for m in instance_patch.take_messages()] == [[0x90, 60, 100]]
    raw_class.on_message = on_message
    instance_patch.set_note_gate(61, True, 0)
    assert instance_patch.take_messages()  # still native, despite a late class monkeypatch

    # Even a spoofed Python marker cannot opt a native instance in without enable().
    spoofed: Any = type("Spoofed", (dsp.MidiGenerator[16],), {"_rackpy_laboratory": True, "on_message": on_message})
    unmarked = spoofed()
    with pytest.raises(LaboratoryError, match="creating a laboratory MIDI generator"):
        unmarked._enable_laboratory()
    unmarked.set_note_gate(60, True, 0)
    assert unmarked.take_messages()

    # A laboratory subclass that deliberately skips its guarded constructor stays native too.
    raw_base: Any = dsp.MidiGenerator[16]
    bypass: Any = type(
        "Bypass", (laboratory.MidiGenerator[16],), {"__init__": raw_base.__init__, "on_message": on_message}
    )
    unmarked = bypass()
    unmarked.set_note_gate(60, True, 0)
    assert unmarked.take_messages()
    assert seen == []


def test_reentrant_same_generator_dispatches_as_cpp() -> None:
    """Native same-object reentry invokes Python again; super records each message once."""
    base: Any = laboratory.MidiGenerator[1]
    seen: list[list[int]] = []

    class Reentrant(base):  # type: ignore[misc]
        def on_message(self, message: Message) -> None:
            seen.append(message.bytes)
            if message.get_status() == 0x9:
                self.set_cc(12, 74)  # native code calls onMessage on this same object again
            super().on_message(message)

    g = Reentrant()
    g.set_note_gate(60, True, 0)
    assert seen == [[0x90, 60, 100], [0xB0, 74, 12]]
    assert [m.bytes for m in g.take_messages()] == [[0xB0, 74, 12], [0x90, 60, 100]]


def test_unbounded_generator_reentry_raises_recursion_error_and_recovers() -> None:
    base: Any = laboratory.MidiGenerator[1]
    seen = 0
    recursive = True

    class Recursive(base):  # type: ignore[misc]
        def on_message(self, message: Message) -> None:
            nonlocal seen
            seen += 1
            if recursive:
                self.set_frame(seen)
                self.set_clock(False)
                self.set_clock(True)  # a new rising edge emits again
            super().on_message(message)

    g = Recursive()
    with pytest.raises(RecursionError, match="overrides nested more than 64"):
        g.set_clock(True)
    assert seen == 64 and g.take_messages() == []
    recursive = False
    g.set_clock(False)
    g.set_clock(True)
    assert seen == 65 and len(g.take_messages()) == 1


def test_private_virtual_test_hooks_are_absent_in_production() -> None:
    if not _rackpy_host.TEST_HOOKS:
        assert not hasattr(_rackpy_host, "_testing")
