# SPDX-License-Identifier: GPL-3.0-or-later
"""rackpy.raw.dsp's ring buffers inside Rack (RP-19C).

The reference (native/tests/dsp_reference.cpp) runs scripts of operations on
Rack's RingBuffer, DoubleRingBuffer and AppleRingBuffer of float, float_4 and
Frame elements, recording the returned elements, size(), empty(), full(),
capacity(), start and end after each step and the data at the end: wrapping
bulk copies, overruns and underruns, appends and reads through end_data and
start_data, and the window moving back along the block. Each script replays
here identically. The other tests check the generic contract's template set,
the refusals, the views and the Frame keys.
"""

import array
import copy
import struct
from collections.abc import Callable
from typing import Any

import pytest

import rackpy.dsp.ringbuffer
from rackpy.raw import dsp
from rackpy.raw.simd import float_4


def f32(bits: str) -> float:
    value: float = struct.unpack("<f", struct.pack("<I", int(bits, 16)))[0]
    return value


def decode(value: Any) -> Any:
    if isinstance(value, dict) and "f" in value:
        return f32(value["f"])
    if isinstance(value, list):
        return [decode(v) for v in value]
    return value


ELEMENTS: dict[str, tuple[Any, int]] = {
    "float": (float, 1),
    "float_4": (float_4, 4),
    "Frame2": (dsp.Frame[2], 2),
    "Frame8": (dsp.Frame[8], 8),
    "Frame16": (dsp.Frame[16], 16),
}


def element(kind: Any, lanes: list[float]) -> Any:
    if kind is float:
        return lanes[0]
    if kind is float_4:
        return float_4(*lanes)
    frame = kind()
    for k, x in enumerate(lanes):
        frame.samples[k] = x
    return frame


def flat(kind: Any, value: Any) -> list[float]:
    if kind is float:
        return [value]
    if kind is float_4:
        result: list[float] = value.tolist()
        return result
    assert type(value) is kind
    samples: list[float] = value.samples.tolist()
    return samples


def view_floats(kind: Any, view: Any, n: int, lanes: int) -> list[float]:
    """The first n elements of a view as their lanes (float_4 views hold vectors; Frame views hold floats)."""
    if kind is float_4:
        return [x for v in view.tolist()[:n] for x in v.tolist()]
    values: list[float] = view.tolist()[: n * lanes]
    return values


def write_view(kind: Any, view: Any, lanes: list[float], width: int) -> None:
    if kind is float_4:
        for k in range(len(lanes) // 4):
            view[k] = float_4(*lanes[4 * k : 4 * k + 4])
    else:
        for k, x in enumerate(lanes):
            view[k] = x
    assert len(lanes) % width == 0


def replay(family: str, kind: Any, lanes: int, args: tuple[int, ...]) -> Callable[[list[Any]], list[Any]]:
    def run(steps: list[Any]) -> list[Any]:
        buf = getattr(dsp, family)[(kind, *args)]()
        out = []
        for step, _ in steps:
            op, rest = step[0], step[1:]
            ret: Any = None
            if op == "push":
                buf.push(element(kind, rest[0]))
            elif op == "shift":
                ret = flat(kind, buf.shift())
            elif op == "clear":
                buf.clear()
            elif op == "return_buffer":
                buf.return_buffer()
            elif op == "start_incr":
                buf.start_incr(rest[0])
            elif op == "push_buffer":
                buf.push_buffer(array.array("f", rest[0]), rest[1])
            elif op == "shift_buffer":
                target = array.array("f", bytes(4 * lanes * rest[0]))
                buf.shift_buffer(target, rest[0])
                ret = target.tolist()
            elif op == "end_write":
                n = rest[1]
                view = buf.end_data() if family == "DoubleRingBuffer" else buf.end_data(n)
                write_view(kind, view, rest[0], lanes)
                buf.end_incr(n)
            elif op == "start_read":
                ret = view_floats(kind, buf.start_data(), rest[0], lanes)
                buf.start_incr(rest[0])
            out.append([step, [ret, buf.size(), buf.empty(), buf.full(), buf.capacity(), buf.start, buf.end]])
        return [out, view_floats(kind, buf.data, len(buf.data), lanes)]

    return run


def calls(fn: str) -> Callable[[list[Any]], list[Any]]:
    family, element_name, *sizes = fn.split(".")
    kind, lanes = ELEMENTS[element_name]
    name = {"ring": "RingBuffer", "doublering": "DoubleRingBuffer", "applering": "AppleRingBuffer"}[family]
    return replay(name, kind, lanes, tuple(int(s) for s in sizes))


def test_every_reference_case_matches_rack(ringbuffer_cases: list[dict[str, Any]]) -> None:
    assert len(ringbuffer_cases) == 9
    mismatches: list[str] = []
    for case in ringbuffer_cases:
        expected = decode(case["result"])
        actual = calls(case["fn"])(expected[0])
        if actual != expected:
            for got, want in zip(actual[0], expected[0], strict=False):
                if got != want:
                    mismatches.append(f"{case['fn']}: at {want[0]}, Rack gives {want[1]}, rackpy.raw gives {got[1]}")
                    break
            else:
                mismatches.append(f"{case['fn']}: the final data differ")
        assert len(case["args"]) == 1 and len(actual[1]) == case["args"][0] * ELEMENTS[case["fn"].split(".")[1]][1]
    assert not mismatches, f"{len(mismatches)} cases differ:\n" + "\n".join(mismatches[:5])


def test_the_generic_contract() -> None:
    assert dsp.RingBuffer[float, 1024] is dsp.RingBuffer_float_1024
    assert dsp.DoubleRingBuffer[float, 1024] is dsp.DoubleRingBuffer_float_1024
    assert dsp.AppleRingBuffer[float, 64, 1024] is dsp.AppleRingBuffer_float_64_1024
    for family in (dsp.RingBuffer, dsp.DoubleRingBuffer):
        assert len(family.specializations) == 5 * 12
        assert family[dsp.Frame[16], 32768].S == 32768 and family[float_4, 16].S == 16
    assert len(dsp.AppleRingBuffer.specializations) == 5 * 9
    assert dsp.AppleRingBuffer[dsp.Frame[2], 4096, 65536].N == 65536
    assert dsp.RingBuffer[dsp.Frame[8], 64] is dsp.RingBuffer[dsp.Frame[8, float], 64]
    for template, bad in (
        (dsp.RingBuffer, (float, 15)),
        (dsp.RingBuffer, (float, 65536)),
        (dsp.RingBuffer, (int, 16)),
        (dsp.RingBuffer, (dsp.Frame[4], 16)),
        (dsp.DoubleRingBuffer, (dsp.Frame[2, float_4], 16)),
        (dsp.AppleRingBuffer, (float, 64, 512)),
        (dsp.AppleRingBuffer, (float, 8192, 131072)),
    ):
        with pytest.raises(TypeError, match="not defined in Rack"):
            template[bad]
    assert rackpy.dsp.ringbuffer.RingBuffer is dsp.RingBuffer


def test_ring_buffer() -> None:
    r = dsp.RingBuffer[float, 16]()
    assert r.empty() and not r.full() and r.size() == 0 and r.capacity() == 16
    assert r.data.tolist() == [0.0] * 16  # zeroed, unlike C++'s default-initialized data
    for k in range(16):
        r.push(float(k))
    assert r.full() and r.capacity() == 0
    r.push(16.0)  # overrun: overwrites the oldest, as in Rack
    assert r.size() == 17 and r.capacity() == 2**64 - 1 and r.shift() == 16.0
    r.clear()
    r.shift()  # underrun: a stale element, and start passes end
    assert r.size() == 2**64 - 1 and r.empty()
    r.start = r.end = 2**64 - 3  # the indices wrap modulo S
    r.push_buffer(array.array("f", range(6)), 6)
    out = array.array("f", bytes(24))
    r.shift_buffer(out, 6)
    assert out.tolist() == [0.0, 1.0, 2.0, 3.0, 4.0, 5.0]
    with pytest.raises(ValueError, match="at most S"):
        r.push_buffer(array.array("f", bytes(68)), 17)
    with pytest.raises(ValueError, match="at most S"):
        r.shift_buffer(array.array("f", bytes(68)), 17)
    with pytest.raises(ValueError, match="negative"):
        r.push_buffer(array.array("f"), -1)
    with pytest.raises(ValueError, match="at least 3 floats"):
        r.push_buffer(array.array("f", [1.0, 2.0]), 3)
    with pytest.raises(TypeError, match="float32"):
        r.push_buffer(array.array("d", [1.0]), 1)
    v = dsp.RingBuffer[float_4, 16]()
    v.push_buffer(array.array("f", range(8)), 2)  # two elements of four lanes
    assert v.shift().tolist() == [0.0, 1.0, 2.0, 3.0]
    frames = dsp.RingBuffer[dsp.Frame[2], 16]()
    frame = dsp.Frame[2]()
    frame.samples[1] = 5.0
    frames.push(frame)
    assert frames.shift().samples.tolist() == [0.0, 5.0] and len(frames.data) == 32
    with pytest.raises(TypeError, match="Frame\\[2\\]"):
        frames.push(dsp.Frame[8]())
    with pytest.raises(TypeError):
        copy.copy(r)  # std::atomic indices: C++ cannot copy it either


def test_double_ring_buffer() -> None:
    r = dsp.DoubleRingBuffer[float, 16]()
    assert len(r.data) == 32
    r.start = r.end = 12
    end = r.end_data()
    assert len(end) == 16 and not end.readonly
    for k in range(8):
        end[k] = float(k)
    r.end_incr(8)  # wraps: Rack copies the tail to the other half
    start = r.start_data()
    assert start.readonly and start.tolist()[:8] == [float(k) for k in range(8)]
    with pytest.raises(TypeError, match="read-only"):
        start[0] = 1.0
    assert r.data.tolist()[:4] == [4.0, 5.0, 6.0, 7.0]
    with pytest.raises(ValueError, match="at most S"):
        r.end_incr(17)
    r.start_incr(100)  # as in Rack: start may pass end
    assert r.empty()
    del r
    assert end.tolist()  # a view keeps its buffer alive
    v = dsp.DoubleRingBuffer[float_4, 16]()
    view = v.start_data()
    assert isinstance(view[0], float_4)
    with pytest.raises(TypeError, match="read-only"):
        view[0] = float_4(1.0)


def test_apple_ring_buffer() -> None:
    r = dsp.AppleRingBuffer[float, 16, 256]()
    assert (r.S, r.N, len(r.data)) == (16, 256, 256)
    for k in range(20):
        r.push(float(k))  # past S, as Rack allows
    assert r.size() == 20 and not r.full() and r.capacity() == 2**64 - 4
    assert r.start_data().tolist() == [float(k) for k in range(20)] and r.start_data().readonly
    r.start_incr(18)
    r.return_buffer()
    assert (r.start, r.end) == (0, 2) and r.data.tolist()[:2] == [18.0, 19.0]
    with pytest.raises(ValueError, match="at most size"):
        r.start_incr(3)
    with pytest.raises(ValueError, match="at most N - size"):
        r.end_data(255)
    window = r.end_data(254)
    window[253] = 1.0
    r.end_incr(254)
    assert r.size() == 256 and r.data[255] == 1.0
    with pytest.raises(ValueError, match="block of N"):
        r.push(0.0)  # Rack would write past the block
    with pytest.raises(ValueError, match="at most N"):
        r.end_incr(1)
    r.start_incr(256)
    with pytest.raises(ValueError, match="empty"):
        r.shift()  # start would pass end
    with pytest.raises(ValueError, match="start must be at most end"):
        r.start = 257
    with pytest.raises(ValueError, match="end must be start"):
        r.end = 257
    r.end = 256
    other = copy.copy(r)  # plain data: copied, as in C++
    other.return_buffer()
    assert (other.start, r.start) == (0, 256)
