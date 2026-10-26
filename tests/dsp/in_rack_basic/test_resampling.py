# SPDX-License-Identifier: GPL-3.0-or-later
"""rackpy.raw.dsp's MinBLEP and resamplers inside Rack (RP-19B).

The reference (native/tests/dsp_reference.cpp) runs Rack's minimum-phase
impulse, MinBLEP generators, speexdsp sample-rate converters, decimators and
upsamplers; each case replays here identically. The other tests check the
template sets, the index and size refusals, read-only sizing fields, the
Frame overload and the speexdsp state handle.
"""

import array
import math
import struct
from collections import Counter
from collections.abc import Callable
from typing import Any

import pytest

import rackpy.dsp.resampling
from rackpy.raw import dsp, simd
from rackpy.raw.simd import float_4, int32_4


def f32(bits: str) -> float:
    value: float = struct.unpack("<f", struct.pack("<I", int(bits, 16)))[0]
    return value


def signed(bits: str) -> int:
    value: int = struct.unpack("<i", struct.pack("<I", int(bits, 16)))[0]
    return value


def decode(value: Any) -> Any:
    if isinstance(value, dict):
        if "f" in value:
            return f32(value["f"])
        if "f4" in value:
            return float_4.cast(int32_4(*map(signed, value["f4"])))
    if isinstance(value, list):
        return [decode(v) for v in value]
    return value


def same(actual: Any, expected: Any) -> bool:
    if isinstance(expected, float):
        if not isinstance(actual, float):
            return False
        if 0 < abs(expected) < 1.1754943508222875e-38:
            return actual in (0.0, expected)
        if math.isnan(expected):
            return math.isnan(actual)
        return actual == expected
    if isinstance(expected, float_4):
        if not isinstance(actual, float_4):
            return False
        return all(same(a, e) for a, e in zip(actual.tolist(), expected.tolist(), strict=True))
    if isinstance(expected, list):
        return (
            isinstance(actual, list | tuple)
            and len(actual) == len(expected)
            and all(same(a, e) for a, e in zip(actual, expected, strict=True))
        )
    return type(actual) is type(expected) and bool(actual == expected)


def floats(n: int) -> array.array[float]:
    return array.array("f", bytes(4 * n))


def impulse(z: int, o: int) -> list[float]:
    out = floats(2 * z * o)
    dsp.min_blep_impulse(z, o, out)
    return out.tolist()


def min_blep(z: int, o: int, vector: bool) -> Callable[[list[Any]], list[Any]]:
    def run(ops: list[Any]) -> list[Any]:
        g = dsp.MinBlepGenerator[z, o, float_4 if vector else float]()
        out = []
        for op in ops:
            if op != "step":
                g.insert_discontinuity(op[0], op[1])
            out.append(g.process())
        return [out, g.pos]

    return run


def converter(n: int) -> Callable[..., list[Any]]:
    def run(
        in_rate: int, out_rate: int, quality: int, channels: int, frames: int, capacity: int, inputs: list[list[float]]
    ) -> Any:
        s = dsp.SampleRateConverter[n]()
        s.set_channels(channels)
        s.set_quality(quality)
        s.set_rates(in_rate, out_rate)
        chunks = []
        for x in inputs:
            out = floats(capacity * n)
            used, written = s.process(array.array("f", x), n, frames, out, n, capacity)
            chunks.append([used, written, out.tolist()])
        return chunks

    return run


def decimator(oversample: int, quality: int, vector: bool) -> Callable[[list[list[Any]]], list[Any]]:
    def run(inputs: list[list[Any]]) -> list[Any]:
        d = dsp.Decimator[oversample, quality, float_4 if vector else float](0.9)
        outs = [d.process(x if vector else array.array("f", x)) for x in inputs]
        return [outs, d.kernel.tolist(), d.in_index]

    return run


def upsampler(oversample: int, quality: int) -> Callable[[list[float]], list[Any]]:
    def run(inputs: list[float]) -> list[Any]:
        u = dsp.Upsampler[oversample, quality](0.75)
        outs = []
        for x in inputs:
            out = floats(oversample)
            u.process(x, out)
            outs.append(out.tolist())
        return [outs, u.in_index]

    return run


CALLS: dict[str, Callable[..., Any]] = {
    "min_blep_impulse": impulse,
    "minblep.16.32": min_blep(16, 32, False),
    "minblep4.8.8": min_blep(8, 8, True),
    "src.1": converter(1),
    "src.2": converter(2),
    "src.16": converter(16),
    "decimator.4.8": decimator(4, 8, False),
    "decimator4.8.16": decimator(8, 16, True),
    "upsampler.4.8": upsampler(4, 8),
    "upsampler.8.4": upsampler(8, 4),
}


def test_every_reference_case_matches_rack(resampling_cases: list[dict[str, Any]]) -> None:
    mismatches: list[str] = []
    seen: Counter[str] = Counter()
    for case in resampling_cases:
        fn = case["fn"]
        seen[fn] += 1
        expected = decode(case["result"])
        actual = CALLS[fn](*decode(case["args"]))
        if not same(actual, expected):
            mismatches.append(f"{fn}: Rack gives {str(expected)[:300]}, rackpy.raw gives {str(actual)[:300]}")
    assert not mismatches, f"{len(mismatches)} cases differ:\n" + "\n".join(mismatches[:5])
    assert set(seen) == set(CALLS), "every reference case is replayed, and nothing else"


def test_template_sets() -> None:
    assert dsp.MinBlepGenerator[16, 32] is dsp.MinBlepGenerator_16_32_float is dsp.MinBlepGenerator[16, 32, float]
    assert dsp.MinBlepGenerator[4, 64, float_4].Z == 4 and dsp.MinBlepGenerator[4, 64, float_4].O == 64
    assert dsp.Decimator[16, 16] is dsp.Decimator_16_16_float and dsp.Decimator[2, 32, float_4].QUALITY == 32
    assert dsp.Upsampler[16, 16] is dsp.Upsampler_16_16 and dsp.SampleRateConverter[16] is dsp.SampleRateConverter_16
    for template, bad in (
        (dsp.MinBlepGenerator, (3, 4)),
        (dsp.MinBlepGenerator, (128, 4)),
        (dsp.Decimator, (3, 8)),
        (dsp.Upsampler, (16, 64)),
        (dsp.SampleRateConverter, (17,)),
        (dsp.SampleRateConverter, (0,)),
    ):
        with pytest.raises(TypeError, match="not defined in Rack"):
            template[bad]
    assert rackpy.dsp.resampling.SampleRateConverter is dsp.SampleRateConverter


def test_min_blep() -> None:
    out = floats(64)
    dsp.min_blep_impulse(4, 8, out)
    assert out[-1] == pytest.approx(1.0) and abs(out[0]) < 0.05  # an integrated step, normalized to 1
    for z, o in ((0, 8), (4, 0), (3, 3), (128, 512)):  # 18 is not a PFFFT length; 131072 is too long
        with pytest.raises(ValueError, match="PFFFT"):
            dsp.min_blep_impulse(z, o, floats(max(2 * z * o, 1)))
    with pytest.raises(ValueError, match="at least 64"):
        dsp.min_blep_impulse(4, 8, floats(63))
    g = dsp.MinBlepGenerator[8, 8]()
    assert len(g.impulse) == 2 * 8 * 8 + 1 and g.impulse[2 * 8 * 8] == 1.0 and len(g.buf) == 16
    g.insert_discontinuity(0.5, 1.0)  # outside -1 < p <= 0: ignored, as in Rack
    assert g.buf.tolist() == [0.0] * 16
    g.insert_discontinuity(-0.5, 1.0)
    assert any(g.buf.tolist())
    g.pos = 15
    with pytest.raises(ValueError, match="0 to 15"):
        g.pos = 16
    v = dsp.MinBlepGenerator[8, 8, float_4]()
    v.insert_discontinuity(-0.25, float_4(1, -1, 2, 0))
    assert isinstance(v.process(), float_4)


def test_sample_rate_converter() -> None:
    s = dsp.SampleRateConverter[2]()
    assert s.channels == 2 and s.quality == 4 and s.in_rate == s.out_rate == 44100 and not s.st  # equal rates: no state
    s.set_rates(44100, 48000)
    assert s.st and s.st == s.st
    with pytest.raises(AttributeError):
        s.channels = 1  # speexdsp's state is sized by it
    for bad in (3, -1):
        with pytest.raises(ValueError, match="0 to 2"):
            s.set_channels(bad)
    with pytest.raises(ValueError, match="0 \\(fastest\\) to 10"):
        s.set_quality(11)
    with pytest.raises(ValueError, match="rate"):
        s.set_rates(0, 48000)
    out = floats(2 * 20)
    with pytest.raises(ValueError, match="stride"):
        s.process(floats(40), 1, 20, out, 2, 20)  # a stride below channels
    with pytest.raises(ValueError, match="at least"):
        s.process(floats(30), 2, 20, out, 2, 20)  # 20 frames of 2 need 40 floats
    # The Frame overload gives the buffer overload's results.
    t = dsp.SampleRateConverter[2]()
    t.set_rates(44100, 48000)
    frames = [dsp.Frame[2]() for _ in range(20)]
    for k, frame in enumerate(frames):
        frame.samples[0] = math.sin(k / 3)
        frame.samples[1] = math.cos(k / 3)
    flat = array.array("f", [x for frame in frames for x in frame.samples.tolist()])
    outs = [dsp.Frame[2]() for _ in range(30)]
    used, written = t.process(frames, 20, outs, 30)
    s2 = dsp.SampleRateConverter[2]()
    s2.set_rates(44100, 48000)
    buffer_out = floats(60)
    assert s2.process(flat, 2, 20, buffer_out, 2, 30) == (used, written)
    assert [x for frame in outs[:written] for x in frame.samples.tolist()] == buffer_out.tolist()[: 2 * written]
    with pytest.raises(TypeError, match="Frame\\[2\\]"):
        t.process([dsp.Frame[3]()], 1, outs, 1)


def test_decimator_and_upsampler() -> None:
    d = dsp.Decimator[4, 8]()
    assert len(d.kernel) == 32 and d.in_index == 0
    assert isinstance(d.process(array.array("f", [1.0, 0.0, 0.0, 0.0])), float) and d.in_index == 4
    with pytest.raises(ValueError, match="at least 4"):
        d.process(array.array("f", [1.0, 2.0]))
    d.in_index = 28
    with pytest.raises(ValueError, match="0 to 28"):
        d.in_index = 29  # Rack would copy past in_buffer
    v = dsp.Decimator[4, 8, float_4]()
    assert isinstance(v.process([float_4(1.0)] * 4), float_4)
    with pytest.raises(ValueError, match="4 vectors"):
        v.process([float_4(1.0)])
    u = dsp.Upsampler[4, 8]()
    out = floats(4)
    u.process(1.0, out)
    assert u.in_index == 1
    with pytest.raises(ValueError, match="at least 4"):
        u.process(1.0, floats(3))
    with pytest.raises(ValueError, match="0 to 7"):
        u.in_index = 8
    assert simd.float_4 is float_4
