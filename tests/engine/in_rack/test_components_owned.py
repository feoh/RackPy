# SPDX-License-Identifier: GPL-3.0-or-later
"""Ports, params and lights that Python owns: Rack's semantics, checked indices, views and deprecations.

test_every_reference_case_matches_rack replays native/tests/components_reference.cpp:
Rack's own Port and Light code over channel counts 0, 1, 2, 5 and 16.
"""

import array
import gc
import math
import struct
import threading
import warnings
from typing import Any

import pytest

from rackpy.raw.engine import PORT_MAX_CHANNELS, Input, Light, LightInfo, Output, Param, Port, PortInfo


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
    # Zeros compare by value: Rack's -fno-signed-zeros makes their sign build-dependent (ADR 009).
    if isinstance(expected, float):
        return isinstance(actual, float) and (math.isnan(actual) if math.isnan(expected) else actual == expected)
    if isinstance(expected, list):
        return (
            isinstance(actual, list | tuple)
            and len(actual) == len(expected)
            and all(map(same, actual, expected, strict=True))
        )
    return type(actual) is type(expected) and bool(actual == expected)


PATTERNS = [[0.0], [1.0], [-2.5, 0.5], [10.0, -10.0, 3.0], [1e-3, 7.0]]


def connected(channels: int, pattern: int) -> Port:
    """A port as Rack leaves it when a cable connects: the raw channel count, then voltages."""
    p = Port()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)
        p.channels = channels  # set_channels does nothing on a disconnected port
    volts = PATTERNS[pattern]
    for c in range(PORT_MAX_CHANNELS):
        # As the C++ reference does it: a float times an int, in float. A product of two float32
        # values is exact in double, so rounding the double product once gives the same result.
        base = struct.unpack("<f", struct.pack("<f", volts[c % len(volts)]))[0]
        p.voltages[c] = base * (c + 1)
    return p


def run_case(fn: str, args: list[Any]) -> Any:
    if fn.startswith("light."):
        light = Light()
        light.set_brightness(args[0])
        if fn == "light.set_brightness_smooth":
            light.set_brightness_smooth(args[1], args[2], args[3])
        else:
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", DeprecationWarning)
                light.set_brightness_smooth(args[1], args[2])
        return light.get_brightness()
    p = connected(args[0], args[1])
    n = range(PORT_MAX_CHANNELS)
    match fn:
        case "port.get_voltage":
            return [p.get_voltage(c) for c in n]
        case "port.get_poly_voltage":
            return [p.get_poly_voltage(c) for c in n]
        case "port.get_normal_voltage":
            return [p.get_normal_voltage(5.0, c) for c in n]
        case "port.get_normal_poly_voltage":
            return [p.get_normal_poly_voltage(5.0, c) for c in n]
        case "port.state":
            return [p.get_channels(), p.is_connected(), p.is_monophonic(), p.is_polyphonic()]
        case "port.get_voltage_sum":
            return p.get_voltage_sum()
        case "port.get_voltage_rms":
            return p.get_voltage_rms()
        case "port.read_voltages":
            return p.read_voltages()
        case "port.set_channels":
            p.set_channels(args[2])
            return [p.get_channels(), p.voltages.tolist()]
        case "port.clear_voltages":
            p.clear_voltages()
            return p.voltages.tolist()
        case "port.write_voltages":
            p.write_voltages([9, 8, 7, 6, 5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -6])
            return p.voltages.tolist()
    raise AssertionError(fn)


def test_every_reference_case_matches_rack(reference: list[dict[str, Any]]) -> None:
    mismatches = []
    for case in reference:
        args, expected = decode(case["args"]), decode(case["result"])
        actual = run_case(case["fn"], args)
        if not same(actual, expected):
            mismatches.append(f"{case['fn']}{tuple(args)}: Rack gives {expected}, rackpy.raw gives {actual}")
    assert not mismatches, f"{len(mismatches)} of {len(reference)} differ:\n" + "\n".join(mismatches[:30])
    assert len(reference) > 400


def test_the_polyphony_limit_is_racks() -> None:
    assert PORT_MAX_CHANNELS == 16 and len(Port().voltages) == PORT_MAX_CHANNELS


def test_channel_indices_are_checked_before_rack_sees_them() -> None:
    p = Port()
    for bad in (-1, 16, 255, 1000):
        with pytest.raises(IndexError, match="channel must be 0 to 15"):
            p.get_voltage(bad)
        with pytest.raises(IndexError):
            p.set_voltage(1.0, bad)
    with pytest.raises(IndexError):
        p.get_voltages(16)
    with pytest.raises(TypeError):
        p.get_voltage(1.5)  # type: ignore[arg-type]


def test_channel_counts_beyond_the_array_are_refused() -> None:
    # Rack's loops (read_voltages, get_voltage_sum, ...) would run past the 16-voltage array.
    p = Port()
    with pytest.raises(ValueError, match="channels must be 0 to 16"):
        p.set_channels(17)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)
        with pytest.raises(ValueError):
            p.channels = 200


def test_set_channels_keeps_racks_rules() -> None:
    p = Port()
    p.set_channels(4)
    assert p.get_channels() == 0 and not p.is_connected()  # disconnected ports stay at 0
    q = connected(3, 3)
    q.set_channels(0)  # 0 means 1, with every voltage cleared above it
    assert q.get_channels() == 1 and q.is_monophonic()


def test_normalling_and_polyphony() -> None:
    p = Port()
    assert p.get_normal_voltage(5.0) == 5.0 and p.get_normal_poly_voltage(2.0, 3) == 2.0
    mono = connected(1, 2)
    assert mono.get_poly_voltage(7) == mono.get_voltage(0)
    poly = connected(4, 2)
    assert poly.get_poly_voltage(3) == poly.get_voltage(3) and poly.is_polyphonic()


def test_read_and_write_voltages_follow_the_channel_count() -> None:
    p = connected(3, 1)
    assert p.read_voltages() == [1.0, 2.0, 3.0]
    with pytest.raises(ValueError, match="at least 3"):
        p.write_voltages([1.0, 2.0])
    p.write_voltages([4.0, 5.0, 6.0, 99.0])
    assert p.voltages.tolist()[:4] == [4.0, 5.0, 6.0, 4.0]  # only `channels` values are written


def test_voltages_is_a_checked_live_view() -> None:
    p = Port()
    v = p.voltages
    v[3] = 1.5
    v[-1] = 2.0
    assert p.get_voltage(3) == 1.5 and p.get_voltage(15) == 2.0 and list(v)[3] == 1.5
    with pytest.raises(IndexError):
        v[16]
    tail = p.get_voltages(14)
    assert len(tail) == 2 and tail[1] == 2.0


def test_owned_ports_share_memory_with_memoryview_and_snapshots_copy() -> None:
    p = Port()
    view = memoryview(p.voltages)
    assert view.format == "f" and len(view) == 16
    view[2] = 4.0  # type: ignore[call-overload]  # typeshed types memoryview items as int; this one is 'f'
    assert p.get_voltage(2) == 4.0
    snap = p.voltages.snapshot()
    assert isinstance(snap, array.array) and snap[2] == 4.0
    p.set_voltage(5.0, 2)
    assert snap[2] == 4.0  # a copy
    del p
    gc.collect()
    assert view[2] == 5.0  # the view keeps the port's storage alive


def test_plug_lights_are_views_of_the_ports_lights() -> None:
    p = Port()
    green, _red, _blue = p.plug_lights
    green.set_brightness(0.5)
    assert p.plug_lights[0] is green and p.plug_lights[0].get_brightness() == 0.5


def test_deprecated_members_warn_and_unstable_ones_work() -> None:
    p = Port()
    with pytest.warns(DeprecationWarning, match="use get_voltage"):
        assert p.value == 0.0
    with pytest.warns(DeprecationWarning):
        p.value = 3.0
    assert p.get_voltage(0) == 3.0  # value is a union with voltages[0]
    with pytest.warns(DeprecationWarning, match="use is_connected"):
        assert p.active == 0
    with pytest.warns(DeprecationWarning, match="use get_normal_voltage"):
        assert p.normalize(2.0) == 2.0
    light = Light()
    with pytest.warns(DeprecationWarning, match="pass delta_time"):
        light.set_brightness_smooth(1.0, 64)
    with pytest.warns(DeprecationWarning, match="use set_brightness_smooth"):
        light.set_smooth_brightness(0.0, 0.1)
    light.value = 0.25
    assert light.get_brightness() == 0.25
    param = Param()
    param.value = 0.75
    assert param.get_value() == 0.75


def test_overloads_of_set_brightness_smooth_follow_cpp() -> None:
    light = Light()
    light.set_brightness(1.0)
    light.set_brightness_smooth(0.0, 0.01)  # a float delta_time: the current overload, lambda 30
    assert light.get_brightness() == pytest.approx(0.7, abs=1e-6)
    with pytest.warns(DeprecationWarning):
        light.set_brightness_smooth(1.0)  # one argument reaches the deprecated per-frame form, as in C++


def test_facade_properties() -> None:
    p = Input()
    p.voltage = 2.5
    assert p.voltage == 2.5 and not p.connected and p.channel_count == 0
    q = connected(2, 1)
    q.channel_count = 4
    assert q.channel_count == 4 and q.polyphonic and not q.monophonic
    light = Light()
    light.brightness = 0.3
    assert light.get_brightness() == pytest.approx(0.3)
    assert isinstance(Input(), Port) and isinstance(Output(), Port)
    assert [t.name for t in Port.Type] == ["INPUT", "OUTPUT"]


def test_info_names_and_python_overrides() -> None:
    info = PortInfo()
    info.name, info.type = "Pitch", Port.Type.INPUT
    assert info.get_full_name() == "Pitch input"
    info.type = Port.Type.OUTPUT
    assert info.get_full_name() == "Pitch output"

    class Named(PortInfo):
        def get_name(self) -> str:
            return "Gate"

        def get_description(self) -> str:
            return "Opens the envelope"

    named = Named()
    assert named.get_full_name() == "Gate input" and named.get_description() == "Opens the envelope"
    light = LightInfo()
    light.name, light.light_id = "Level", 2
    assert light.get_name() == "Level" and light.get_description() == ""


def test_owned_components_work_from_python_threads() -> None:
    p = Port()
    errors: list[BaseException] = []

    def worker() -> None:
        try:
            for i in range(5000):
                p.set_voltage(float(i), i % 16)
                p.get_voltage(i % 16)
        except BaseException as e:
            errors.append(e)

    threads = [threading.Thread(target=worker) for _ in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert errors == []
