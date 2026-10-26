# SPDX-License-Identifier: GPL-3.0-or-later
"""rackpy.raw.dsp's FFTs, FIR helpers and real-time convolver inside Rack (RP-19A).

The reference (native/tests/dsp_reference.cpp) runs Rack's PFFFT wrappers,
convolutions and the convolver; each case replays here identically. The
other tests check PFFFT's length rules, buffer checks (type, size, alignment,
overlap), normalization and ordering, the setup handles and the convolver's
views.
"""

import array
import copy
import math
import struct
from collections import Counter
from collections.abc import Callable
from typing import Any

import pytest

import rackpy.dsp.fft
from rackpy.raw import dsp


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
        if 0 < abs(expected) < 1.1754943508222875e-38:
            return actual in (0.0, expected)
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


def floats(n: int) -> array.array[float]:
    return array.array("f", bytes(4 * n))


def real_fft(n: int) -> Callable[[list[float]], list[list[float]]]:
    def run(x: list[float]) -> list[list[float]]:
        fft = dsp.RealFFT(n)
        source = array.array("f", x)
        out, back, ordered, inverse = floats(n), floats(n), floats(n), floats(n)
        fft.rfft_unordered(source, out)
        fft.irfft_unordered(out, back)
        fft.rfft(source, ordered)
        fft.irfft(ordered, inverse)
        scaled = array.array("f", inverse)
        fft.scale(scaled)
        return [b.tolist() for b in (out, back, ordered, inverse, scaled)]

    return run


def complex_fft(n: int) -> Callable[[list[float]], list[list[float]]]:
    def run(x: list[float]) -> list[list[float]]:
        fft = dsp.ComplexFFT(n)
        source = array.array("f", x)
        out, back, ordered, inverse = floats(2 * n), floats(2 * n), floats(2 * n), floats(2 * n)
        fft.fft_unordered(source, out)
        fft.ifft_unordered(out, back)
        fft.fft(source, ordered)
        fft.ifft(ordered, inverse)
        scaled = array.array("f", inverse)
        fft.scale(scaled)
        return [b.tolist() for b in (out, back, ordered, inverse, scaled)]

    return run


def boxcar(length: int, cutoff: float) -> list[float]:
    out = array.array("f", [7.0] * max(length, 1))
    dsp.boxcar_lowpass_ir(out, length, cutoff)
    return out.tolist()


def convolver(block: int, kernel: list[float], inputs: list[list[float]]) -> list[Any]:
    conv = dsp.RealTimeConvolver(block)
    conv.set_kernel(array.array("f", kernel) if kernel else None, len(kernel))
    outputs = []
    for x in inputs:
        out = floats(block)
        conv.process_block(array.array("f", x), out)
        outputs.append(out.tolist())
    return [outputs, conv.kernel_blocks, conv.input_pos]


CALLS: dict[str, Callable[..., Any]] = {
    "convolve_naive": lambda x, k, n: dsp.convolve_naive(array.array("f", x), array.array("f", k), n),
    "boxcar_lowpass_ir": boxcar,
    "convolver": convolver,
}
for _n in (32, 96, 160, 1024):
    CALLS[f"rfft.{_n}"] = real_fft(_n)
for _n in (16, 48, 80, 512):
    CALLS[f"cfft.{_n}"] = complex_fft(_n)


def test_every_reference_case_matches_rack(advanced_cases: list[dict[str, Any]]) -> None:
    mismatches: list[str] = []
    seen: Counter[str] = Counter()
    for case in advanced_cases:
        fn = case["fn"]
        seen[fn] += 1
        expected = decode(case["result"])
        actual = CALLS[fn](*decode(case["args"]))
        if not same(actual, expected):
            mismatches.append(f"{fn}: Rack gives {str(expected)[:300]}, rackpy.raw gives {str(actual)[:300]}")
    assert not mismatches, f"{len(mismatches)} cases differ:\n" + "\n".join(mismatches[:5])
    assert set(seen) == set(CALLS), "every reference case is replayed, and nothing else"


def test_pffft_lengths_are_checked_before_rack_sees_them() -> None:
    for n in (32, 96, 160, 1024, 65536):
        assert dsp.RealFFT(n).length == n
    for n in (0, -32, 16, 48, 31, 224, 2 * 65536):  # 48 is not a multiple of 32; 224 has a factor 7
        with pytest.raises(ValueError):
            dsp.RealFFT(n)
    for n in (16, 48, 80):
        assert dsp.ComplexFFT(n).length == n
    for n in (8, 24, 112, 2 * 65536):
        with pytest.raises(ValueError):
            dsp.ComplexFFT(n)


def test_normalization_ordering_and_known_bins() -> None:
    n = 32
    fft = dsp.RealFFT(n)
    x = array.array("f", [math.cos(2 * math.pi * 3 * k / n) for k in range(n)])
    spectrum = floats(n)
    fft.rfft(x, spectrum)
    assert spectrum[2 * 3] == pytest.approx(n / 2, abs=1e-4) and spectrum[2 * 3 + 1] == pytest.approx(0.0, abs=1e-4)
    assert spectrum[0] == pytest.approx(0.0, abs=1e-4)  # F(0) in the first slot, F(n/2) in the second
    back = floats(n)
    fft.irfft(spectrum, back)
    assert back[1] == pytest.approx(n * x[1], rel=1e-4)  # unscaled: irfft(rfft(x)) = n x
    fft.scale(back)
    assert back.tolist() == pytest.approx(x.tolist(), abs=1e-5)
    fft.rfft(x, x)  # input and output may be the same buffer
    assert x.tolist() == spectrum.tolist()
    c = dsp.ComplexFFT(16)
    impulse = floats(32)
    impulse[0] = 1.0
    out = floats(32)
    c.fft(impulse, out)
    assert out.tolist() == [1.0, 0.0] * 16  # a flat spectrum


def test_buffers_are_checked() -> None:
    fft = dsp.RealFFT(32)
    good = floats(32)
    with pytest.raises(ValueError, match="at least 32"):
        fft.rfft(floats(31), good)
    with pytest.raises(TypeError, match="float32"):
        fft.rfft(array.array("d", [0.0] * 32), good)
    misaligned = memoryview(floats(33))[1:]
    with pytest.raises(ValueError, match="16-byte"):
        fft.rfft(misaligned, good)
    shared = floats(40)
    with pytest.raises(ValueError, match="overlap"):
        fft.rfft(memoryview(shared)[:32], memoryview(shared)[4:36])
    with pytest.raises(BufferError):
        fft.rfft(good, memoryview(floats(32)).toreadonly())
    with pytest.raises(ValueError, match="contiguous"):
        fft.rfft(memoryview(floats(64))[::2], good)
    fft.length = 64  # scale uses the field, the transforms the setup's length
    with pytest.raises(ValueError, match="at least 64"):
        fft.scale(good)
    fft.rfft(good, good)


def test_setup_handles_and_ownership() -> None:
    fft = dsp.RealFFT(64)
    setup = fft.setup
    assert setup and setup == fft.setup and hash(setup) == hash(fft.setup) and setup != dsp.RealFFT(64).setup
    del fft
    assert setup  # the handle keeps its FFT alive
    with pytest.raises(AttributeError):
        dsp.ComplexFFT(16).setup = setup  # type: ignore[misc]  # the FFT owns its setup
    with pytest.raises(TypeError):
        copy.copy(dsp.RealFFT(32))  # a copy would free the setup twice in C++


def test_real_time_convolver() -> None:
    for bad in (8, 24, 33, 2 * 32768):
        with pytest.raises(ValueError, match="block_size"):
            dsp.RealTimeConvolver(bad)
    conv = dsp.RealTimeConvolver(32)
    assert conv.block_size == 32 and conv.kernel_blocks == 0 and conv.pffft
    assert len(conv.kernel_ffts) == 0 and len(conv.output_tail) == 32 and len(conv.tmp_block) == 64
    view = conv.kernel_ffts
    kernel = array.array("f", [1.0] + [0.0] * 63)  # an identity, two blocks long
    conv.set_kernel(kernel, 64)
    assert conv.kernel_blocks == 2 and len(view) == 2 * 64  # the view finds the new array
    block = array.array("f", [float(k) for k in range(32)])
    conv.process_block(block, block)  # in place: input is read first
    assert block.tolist() == pytest.approx([float(k) for k in range(32)], abs=1e-4)
    conv.input_pos = 7
    assert conv.input_pos == 7
    with pytest.raises(ValueError):
        conv.input_pos = -1
    with pytest.raises(AttributeError):
        conv.block_size = 64  # type: ignore[misc]  # the arrays are sized by it
    with pytest.raises(ValueError):
        conv.set_kernel(kernel, 65)  # longer than the buffer
    conv.set_kernel(None, 0)
    out = floats(32)
    conv.process_block(block, out)
    assert out.tolist() == [0.0] * 32 and len(view) == 0
    with pytest.raises(IndexError):
        view[0]
    assert rackpy.dsp.fft.RealTimeConvolver is dsp.RealTimeConvolver
