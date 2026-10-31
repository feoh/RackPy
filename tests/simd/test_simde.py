# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-20, coverage policy 12: Rack's SSE4.2 and SIMDe implementations of rack::simd, compared.

simd/common.hpp uses SSE4.2 when the compiler targets it (Rack's x86-64
builds) and SIMDe otherwise (Rack's arm64 builds). The same reference
program (native/tests/simd_reference.cpp) is built both ways; the SIMDe build
uses SIMDe's portable C code (SIMDE_NO_NATIVE), which is not the NEON code an
arm64 build runs, so this documents Rack's two source paths on x86-64; it
does not prove anything about arm64. The envelope below is what
docs/reference/simd.md promises.
"""

import json
import math
import struct
import subprocess
from collections import Counter
from pathlib import Path
from typing import Any

import pytest

from tests.support import PLATFORM, rack_platforms

pytestmark = [pytest.mark.native, rack_platforms()]

# sse_mathfun's polynomials and everything built on them: the SIMDe build rounds differently in the
# last bits (at most a few ULP, so far 1e-5 relative after pow's amplification).
TRANSCENDENTAL = {
    "log",
    "log10",
    "log2",
    "exp",
    "sin",
    "cos",
    "tan",
    "atan",
    "atan2",
    "pow",
    "pow_s",
    "arg_c",
    "sse_mathfun_log_ps",
    "sse_mathfun_exp_ps",
    "sse_mathfun_sin_ps",
    "sse_mathfun_cos_ps",
    "sse_mathfun_sincos_ps",
    "sse_mathfun_tan_ps",
    "sse_mathfun_cot_ps",
    "sse_mathfun_tancot_ps",
    "sse_mathfun_atan_ps",
    "sse_mathfun_atan2_ps",
}
RELATIVE = 1e-5
RCP_BOUND = 1.5 * 2**-12  # Intel's documented bound for rcpps; SIMDe's portable rcp is exact


def f32(bits: str) -> float:
    value: float = struct.unpack("<f", struct.pack("<I", int(bits, 16)))[0]
    return value


def lanes(value: Any) -> list[tuple[str, Any]]:
    if isinstance(value, dict):
        if "f4" in value:
            return [("f", b) for b in value["f4"]]
        if "f" in value:
            return [("f", value["f"])]
        if "i4" in value:
            return [("i", x) for x in value["i4"]]
    if isinstance(value, list):
        return [lane for v in value for lane in lanes(v)]
    return [("i", value)]


def subnormal(bits: str) -> bool:
    magnitude = int(bits, 16) & 0x7FFFFFFF
    return 0 < magnitude < 0x00800000


def classify(fn: str, args: Any, k: int, expected: str, actual: str) -> str:
    """Why lane k of a case differs, or 'unexplained'."""
    e, a = f32(expected), f32(actual)
    inputs = lanes(args)
    if e == 0.0 and a == 0.0:
        return "zero-sign"  # not Rack semantics: -funsafe-math-optimizations implies -fno-signed-zeros
    if math.isnan(e) and math.isnan(a):
        return "nan-payload"
    if subnormal(expected) or subnormal(actual) or any(t == "f" and subnormal(b) for t, b in inputs):
        return "subnormal"  # SSE's flush-to-zero and denormals-are-zero apply differently to SIMDe's code
    if fn == "rsqrt":
        return "rsqrt"
    if fn == "rcp":
        return "rcp" if abs(a - e) <= RCP_BOUND * abs(e) else "unexplained"
    if fn in TRANSCENDENTAL:
        if math.isfinite(e) and math.isfinite(a) and abs(a - e) <= RELATIVE * abs(e):
            return "last-bits"
        lane_in = inputs[k % 4] if inputs and inputs[0][0] == "f" else None
        if lane_in and math.isinf(f32(lane_in[1])):
            return "infinite-input"  # SSE gives sin(+-inf) = -+inf; SIMDe gives NaN
    return "unexplained"


def run(binary: Path) -> dict[str, Any]:
    data: dict[str, Any] = json.loads(subprocess.run([str(binary)], capture_output=True, check=True, text=True).stdout)
    return data


@pytest.mark.skipif(
    PLATFORM == "mac-arm64",
    reason="arm64 has no SSE4.2: both builds are SIMDe there, so there are not two paths to compare; "
    "tests/simd/test_simd.py checks rack::simd in Rack on arm64",
)
def test_sse42_and_simde_agree_within_the_documented_envelope(dev_build: Path) -> None:
    sse = run(dev_build / "rackpy_simd_reference")
    simde = run(dev_build / "rackpy_simd_reference_simde")
    assert (sse["implementation"], simde["implementation"]) == ("sse4.2", "simde")
    assert len(sse["cases"]) == len(simde["cases"]) > 20000
    reasons: Counter[str] = Counter()
    unexplained: list[str] = []
    for x, y in zip(sse["cases"], simde["cases"], strict=True):
        assert (x["fn"], x["args"]) == (y["fn"], y["args"]), "both builds run the same cases in the same order"
        for k, ((kind, expected), (_, actual)) in enumerate(zip(lanes(x["result"]), lanes(y["result"]), strict=True)):
            if expected == actual:
                continue
            reason = "unexplained" if kind == "i" else classify(x["fn"], x["args"], k, expected, actual)
            reasons[f"{x['fn']}: {reason}"] += 1
            if reason == "unexplained":
                unexplained.append(f"{x['fn']}{x['args']}: SSE4.2 {x['result']}, SIMDe {y['result']}")
    assert not unexplained, f"{len(unexplained)} lanes differ outside the envelope:\n" + "\n".join(unexplained[:20])
    # Integer results, masks, conversions, loads and stores agree exactly; so does all bitwise logic.
    assert not any(key.split(":")[0].startswith("i4.") for key in reasons)
    # The documented big difference is real: SIMDe's portable rsqrt is not the CPU's approximation.
    assert reasons["rsqrt: rsqrt"] > 0
