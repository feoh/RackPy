# SPDX-License-Identifier: GPL-3.0-or-later
"""rackpy.raw.dsp's VU meters and MIDI generator and parser inside Rack (RP-18D).

The reference (native/tests/dsp_reference.cpp) runs op scripts through Rack's
objects, recording the messages generated or the parser's whole state after
each op; each script replays here identically. The other tests check the
refusals RackPy adds where Rack's code would index out of bounds, divide by
zero or make an invalid enum, and the templates, views and deprecations.
"""

import json
import math
import struct
import warnings
from collections import Counter
from collections.abc import Callable
from typing import Any

import pytest

import rackpy.dsp.midi
from rackpy.raw import dsp, midi


def f32(bits: str) -> float:
    value: float = struct.unpack("<f", struct.pack("<I", int(bits, 16)))[0]
    return value


def decode(value: Any) -> Any:
    if isinstance(value, dict) and "f" in value:
        return f32(value["f"])
    if isinstance(value, list):
        return [decode(v) for v in value]
    return value


def same(actual: Any, expected: Any) -> bool:
    if isinstance(expected, float):
        if not isinstance(actual, float):
            return False
        if math.isnan(expected):
            return math.isnan(actual)
        return actual == expected
    if isinstance(expected, list):
        return (
            isinstance(actual, list | tuple)
            and len(actual) == len(expected)
            and all(same(a, e) for a, e in zip(actual, expected, strict=True))
        )
    return type(actual) is type(expected) and bool(actual == expected)


def first_difference(actual: Any, expected: Any, path: str = "") -> str:
    if isinstance(expected, list) and isinstance(actual, list | tuple) and len(actual) == len(expected):
        for k, (a, e) in enumerate(zip(actual, expected, strict=True)):
            if not same(a, e):
                return first_difference(a, e, f"{path}[{k}]")
    return f"at {path or 'the top'}: Rack gives {expected!r}, rackpy.raw gives {actual!r}"


def old_meter(values: list[float]) -> list[list[Any]]:
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)
        meter = dsp.VuMeter()
    out = []
    for v in values:
        meter.set_value(v)
        out.append([meter.db_scaled, [meter.get_brightness(k) for k in range(7)]])
    return out


def meter2(mode: int, dt: float, inputs: list[float]) -> list[list[float]]:
    meter = dsp.VuMeter2()
    meter.mode = dsp.VuMeter2.Mode(mode)
    out = []
    for k, x in enumerate(inputs):
        meter.process(dt, x)
        out.append(
            [
                meter.v,
                meter.get_brightness(-12.0, -6.0),
                meter.get_brightness(-6.0, 0.0),
                meter.get_brightness(0.0, 0.0),
            ]
        )
        if k == 40:
            meter.reset()
    return out


def generator(channels: int) -> Callable[[list[list[Any]]], list[list[Any]]]:
    def run(ops: list[list[Any]]) -> list[list[Any]]:
        g = dsp.MidiGenerator[channels]()
        setters = {
            "vel": g.set_velocity,
            "gate": g.set_note_gate,
            "kp": g.set_key_pressure,
            "cp": g.set_channel_pressure,
            "cc": g.set_cc,
            "mod": g.set_mod_wheel,
            "vol": g.set_volume,
            "bal": g.set_balance,
            "pan": g.set_pan,
            "sus": g.set_sustain_pedal,
            "pw": g.set_pitch_wheel,
            "clk": g.set_clock,
            "start": g.set_start,
            "cont": g.set_continue,
            "stop": g.set_stop,
            "frame": g.set_frame,
            "channels": g.set_channels,
            "panic": g.panic,
            "reset": g.reset,
        }
        out = []
        for name, *args in ops:
            setters[name](*args)
            out.append([[m.bytes, m.frame] for m in g.take_messages()])
        return out

    return run


def state(p: Any) -> list[Any]:
    n = p.MAX_CHANNELS
    wheels = [[p.get_pw(c), p.get_mod(c)] for c in range(p.get_wheel_channels())]
    pulses = [p.clock_pulse, p.clock_divider_pulse, p.start_pulse, p.stop_pulse, p.continue_pulse]
    return [
        p.notes.tolist(),
        p.gates.tolist(),
        p.velocities.tolist(),
        p.aftertouches.tolist(),
        p.pws.tolist(),
        p.mods.tolist(),
        p.held_notes,
        p.rotate_index,
        p.clock,
        p.pedal,
        p.get_channels(),
        [p.get_pitch_voltage(c) for c in range(n)],
        wheels,
        [pulse.remaining for pulse in pulses],
        [pulse.remaining for pulse in p.retrigger_pulses.tolist()],
    ]


def parser(channels: int) -> Callable[[list[list[Any]]], list[Any]]:
    def run(ops: list[list[Any]]) -> list[Any]:
        p = dsp.MidiParser[channels]()
        out = []
        for name, *args in ops:
            if name == "channels":
                p.set_channels(args[0])
            elif name == "poly":
                p.set_poly_mode(dsp.MidiParser_16.PolyMode(args[0]))
            elif name == "mono":
                p.set_mono_mode(dsp.MidiParser_16.MonoMode(args[0]))
            elif name == "flags":
                p.retrigger_on_resume, p.release_velocity_enabled, p.clock_division = args
            elif name == "msg":
                m = midi.Message()
                m.bytes = args[0]
                m.frame = 7
                p.process_message(m)
            elif name == "step":
                p.process_filters(args[0])
                p.process_pulses(args[1])
            out.append(state(p))
        return [out, p.to_json()]

    return run


FROM_JSON = {
    "channels": 3,
    "monoMode": 2,
    "retriggerOnResume": True,
    "polyMode": 1,
    "releaseVelocityEnabled": True,
    "pwRange": 12.5,
    "smooth": False,
    "clockDivision": 12,
    "lastPitch": 10000,
    "lastMod": 64,
    "filterLambda": 5.0,
}


def from_json() -> list[Any]:
    p = dsp.MidiParser[16]()
    p.from_json(FROM_JSON)
    fields = [p.mono_mode.value, p.poly_mode.value, p.retrigger_on_resume, p.release_velocity_enabled, p.pw_range]
    return [state(p), *fields, p.smooth, p.clock_division, p.pw_filters[0].lambda_]


CALLS: dict[str, Callable[..., Any]] = {
    "vumeter.old": old_meter,
    "vumeter2": meter2,
    "generator.16": generator(16),
    "generator.4": generator(4),
    "generator.1": generator(1),
    "parser.from_json": from_json,
}
for _name in ("mono_last", "mono_first", "mono_lowest", "mono_highest", "rotate", "reuse", "reset", "mpe"):
    CALLS[f"parser.16.{_name}"] = parser(16)
CALLS["parser.4.rotate"] = parser(4)
CALLS["parser.1.mono"] = parser(1)


def test_every_reference_script_matches_rack(midi_cases: list[dict[str, Any]]) -> None:
    mismatches: list[str] = []
    seen: Counter[str] = Counter()
    for case in midi_cases:
        fn = case["fn"]
        seen[fn] += 1
        expected = decode(case["result"])
        if fn.startswith("parser.") and fn != "parser.from_json":
            expected[1] = json.loads(expected[1])  # Rack's toJson, as text
        actual = CALLS[fn](*decode(case["args"]))
        if not same(actual, expected):
            mismatches.append(f"{fn}: {first_difference(actual, expected)}")
    assert not mismatches, f"{len(mismatches)} scripts differ:\n" + "\n".join(m[:1500] for m in mismatches[:5])
    assert set(seen) == set(CALLS), "every reference script is replayed, and nothing else"


def test_vu_meters() -> None:
    with pytest.warns(DeprecationWarning, match="VuMeter2"):
        old = dsp.VuMeter()
    assert old.db_scaled == 0.0 and old.db_interval == 3.0  # db_scaled is 0 until set_value (Rack leaves it unset)
    with pytest.warns(DeprecationWarning, match="typedef"):
        assert dsp.VUMeter is dsp.VuMeter
    meter = dsp.VuMeter2()
    assert meter.mode is dsp.VuMeter2.Mode.PEAK and meter.lambda_ == 30.0
    meter.process(0.001, -2.0)
    assert meter.v == 2.0 and meter.get_brightness(-6.0, 0.0) == 1.0  # a peak jumps up
    meter.mode = dsp.VuMeter2.Mode.RMS
    meter.reset()
    meter.process(0.01, 1.0)
    assert meter.v == pytest.approx(0.3)  # mean square: (1 - 0) * 30 * 0.01
    assert rackpy.dsp.midi.VuMeter2 is dsp.VuMeter2


def test_midi_generator() -> None:
    assert dsp.MidiGenerator[16] is dsp.MidiGenerator_16 and dsp.MidiGenerator[1].CHANNELS == 1
    for bad in (0, 17, "16"):
        with pytest.raises(TypeError, match="not defined in Rack"):
            dsp.MidiGenerator[bad]
    g = dsp.MidiGenerator[4]()
    assert g.channels == 4 and g.vels.tolist() == [100] * 4 and g.ccs[0] == -1 and g.pw == 0x2000 and g.frame == -1
    g.set_note_gate(60, True, 3)
    messages = g.take_messages()
    assert len(messages) == 1 and isinstance(messages[0], midi.Message) and messages[0].bytes == [0x90, 60, 100]
    assert g.take_messages() == []  # taking clears them
    out_of_range: list[Callable[[], object]] = [
        lambda: g.set_note_gate(60, True, 4),
        lambda: g.set_velocity(1, -1),
        lambda: g.set_cc(1, 128),
    ]
    for call in out_of_range:
        with pytest.raises(IndexError):
            call()
    with pytest.raises(ValueError, match="0 to 4"):
        g.set_channels(5)
    with pytest.raises(OverflowError):
        g.set_pitch_wheel(2**15)
    with pytest.raises(OverflowError):
        g.set_velocity(128, 0)
    g.panic()
    assert len(g.take_messages()) == 128  # a note off for every note
    assert not hasattr(g, "on_message")  # Rack's onMessage is an engine callback: RackPy records instead
    with pytest.raises(TypeError, match="on_message"):

        class Override(dsp.MidiGenerator_16):
            def on_message(self, message: midi.Message) -> None: ...


def test_midi_parser_refuses_what_rack_would_get_wrong() -> None:
    p = dsp.MidiParser[4]()
    assert p.MAX_CHANNELS == 4 and dsp.MidiParser[4].MonoMode is dsp.MidiParser_16.MonoMode
    for bad in (4, -1, 200):
        with pytest.raises((IndexError, OverflowError)):
            p.get_pitch_voltage(bad)
        with pytest.raises((IndexError, OverflowError)):
            p.get_pw(bad)
    with pytest.raises(ValueError, match="0 to 4"):
        p.set_channels(5)
    with pytest.raises(ValueError, match="0 to 4"):
        p.channels = 5
    # MPE indexes the 4-channel arrays by the MIDI channel: channel 9 would be out of bounds.
    p.set_channels(4)
    p.set_poly_mode(dsp.MidiParser_16.PolyMode.MPE_MODE)
    on9 = midi.Message()
    on9.bytes = [0x99, 60, 100]
    with pytest.raises(ValueError, match="MIDI channel 9"):
        p.process_message(on9)
    with pytest.raises(IndexError):
        p.press_note(60, 9, 100)
    on2 = midi.Message()
    on2.bytes = [0x92, 60, 100]
    p.process_message(on2)
    assert p.gates[2] and p.notes[2] == 60
    # A timing clock divides the clock by clock_division.
    p.clock_division = 0
    clock = midi.Message()
    clock.bytes = [0xF8]
    with pytest.raises(ValueError, match="divide"):
        p.process_message(clock)
    # Patch JSON is untrusted.
    for invalid in ({"channels": 99}, {"monoMode": 9}, {"polyMode": -1}):
        with pytest.raises(ValueError):
            p.from_json(invalid)
    p.from_json({"channels": 2, "clockDivision": 3})
    assert p.channels == 2 and p.clock_division == 3


def test_midi_parser_state_and_views() -> None:
    p = dsp.MidiParser[16]()
    assert p.to_json()["clockDivision"] == 24 and p.get_wheel_channels() == 1
    p.held_notes = [60, 62]
    assert p.held_notes == [60, 62]
    with pytest.raises(OverflowError):
        p.held_notes = [256]
    p.notes[0] = 72  # a live view
    assert p.get_pitch_voltage(0) == 1.0
    p.pw_filters[0].out = 0.5  # the filters are live too
    assert p.get_pw(0) == 0.5 and p.get_pitch_voltage(0) == pytest.approx(1.0 + 0.5 * 2 / 12)
    p.clock_pulse.trigger(0.01)
    assert p.clock_pulse.is_high()
    p.process_pulses(0.02)
    assert not p.clock_pulse.is_high()
    with pytest.raises(TypeError):
        p.gates[0] = 1  # bools, not ints
    assert rackpy.dsp.midi.MidiParser is dsp.MidiParser
