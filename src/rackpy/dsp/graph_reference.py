# SPDX-License-Identifier: GPL-3.0-or-later
"""The offline reference for DSP graphs (RP-21): a plan, evaluated in Python.

This is **not** how a graph runs in a module. It evaluates a
:class:`~rackpy.dsp.graph.Plan` one frame at a time in pure Python, for
tests, teaching and checking a graph before using it: thousands of times
slower than the native tiers, and never on Rack's engine thread.

It follows the plan's semantics with float32 arithmetic: every operation is
rounded to float32, as the native nodes compute. Addition, subtraction,
multiplication, division, comparisons, ``min``, ``max`` and ``clamp`` match
float32 hardware exactly; ``sin2pi`` and ``pitch_to_freq`` round Python's
double-precision ``sin`` and ``pow``, which can differ from the C library's
float functions by one unit in the last place. Triggers and pulses follow
Rack's ``dsp::SchmittTrigger`` and ``dsp::PulseGenerator``, and outputs follow
``Port::setChannels`` on a patched output (at least one channel).

This module is pure Python and works outside Rack.
"""

from __future__ import annotations

import ctypes
import dataclasses
import math
from collections.abc import Callable, Mapping, Sequence

from rackpy.dsp.graph import MAX_CHANNELS, Graph, GraphError, Plan

__all__ = ["Result", "run"]

FREQ_C4: float = ctypes.c_float(261.6256).value
"""``rack::dsp::FREQ_C4``, as the float it is."""


def f32(x: float) -> float:
    """``x`` rounded to float32 (overflow gives an infinity, as in C)."""
    return ctypes.c_float(x).value


_TWO_PI = f32(2.0 * f32(math.pi))  # 2.f * float(M_PI)
_LOW, _HIGH, _UNINITIALIZED = 0, 1, 2


@dataclasses.dataclass
class Result:
    """What a run wrote: each output port's voltages per frame (one list of channels each), each light's brightness."""

    outputs: dict[int, list[list[float]]]
    lights: dict[int, list[float]]


def _lanes(signal: list[float], n: int) -> list[float]:
    if len(signal) == 1:
        return signal * n
    return [signal[c] if c < len(signal) else 0.0 for c in range(n)]


def _fmin(a: float, b: float) -> float:
    if math.isnan(a):
        return b
    if math.isnan(b):
        return a
    return min(a, b)


def _fmax(a: float, b: float) -> float:
    if math.isnan(a):
        return b
    if math.isnan(b):
        return a
    return max(a, b)


def _div(a: float, b: float) -> float:
    """IEEE division: Python raises on a zero divisor, C does not."""
    if b != 0 or math.isnan(b):
        return a / b
    if a == 0 or math.isnan(a):
        return math.nan
    return math.copysign(math.inf, a) * math.copysign(1.0, b)


def _pow2(x: float) -> float:
    try:
        return f32(math.pow(2.0, x))
    except OverflowError:
        return math.inf


def _sin(x: float) -> float:
    return f32(math.sin(x)) if math.isfinite(x) else math.nan


_ELEMENTWISE: dict[str, Callable[..., float]] = {
    "add": lambda a, b: a + b,
    "sub": lambda a, b: a - b,
    "mul": lambda a, b: a * b,
    "div": _div,
    "min": _fmin,
    "max": _fmax,
    "lt": lambda a, b: 1.0 if a < b else 0.0,
    "le": lambda a, b: 1.0 if a <= b else 0.0,
    "gt": lambda a, b: 1.0 if a > b else 0.0,
    "ge": lambda a, b: 1.0 if a >= b else 0.0,
    "neg": lambda x: -x,
    "abs": abs,
    "clamp": lambda x, low, high: _fmax(_fmin(x, high), low),
    "where": lambda c, a, b: a if c > 0 else b,
    "pitch_to_freq": lambda v: FREQ_C4 * _pow2(v),
    "sin2pi": lambda x: _sin(f32(_TWO_PI * x)),
}


def _elementwise(op: str, operands: list[list[float]]) -> list[float]:
    n = max(len(s) for s in operands)
    columns = [_lanes(s, n) for s in operands]
    fn = _ELEMENTWISE[op]
    return [f32(fn(*xs)) for xs in zip(*columns, strict=True)]


def run(
    graph: Graph | Plan,
    frames: int,
    *,
    sample_rate: float = 44100.0,
    rate_changes: Mapping[int, float] | None = None,
    inputs: Mapping[int, Sequence[Sequence[float]]] | None = None,
    params: Mapping[int, float | Sequence[float]] | None = None,
    controls: Mapping[int, Mapping[str, float]] | None = None,
) -> Result:
    """Evaluates ``graph`` for ``frames`` frames.

    - ``inputs``: each patched input port's voltages, one list of channels (0
      to 16) per frame. Ports not given are unpatched (0 channels).
    - ``params``: each parameter's value, constant or one per frame; 0 if not given.
    - ``controls``: control changes, by frame: ``{100: {"gain": 0.5}}`` sets
      ``gain`` before frame 100 runs.
    - ``rate_changes``: new sample rates, by the frame they apply from.
    """
    plan = graph.plan() if isinstance(graph, Graph) else graph
    inputs = dict(inputs or {})
    params = dict(params or {})
    for port, values in inputs.items():
        if len(values) != frames:
            raise ValueError(f"input {port} has {len(values)} frames of voltages; the run has {frames}")
        if any(len(v) > MAX_CHANNELS for v in values):
            raise ValueError(f"input {port} has a frame of more than {MAX_CHANNELS} channels")
    for index, value in params.items():
        if not isinstance(value, int | float) and len(value) != frames:
            raise ValueError(f"param {index} has {len(value)} values; the run has {frames}")
    names = {c.name: c.node for c in plan.controls}
    for frame, changes in (controls or {}).items():
        unknown = set(changes) - set(names)
        if unknown:
            raise GraphError(f"frame {frame} sets unknown controls {sorted(unknown)}; the plan has {sorted(names)}")

    nodes = plan.nodes
    current: dict[int, float] = {k: f32(n.args[0]) for k, n in enumerate(nodes) if n.op == "control"}
    phases = {k: [0.0] * MAX_CHANNELS for k, n in enumerate(nodes) if n.op == "phasor"}
    triggers = {k: [_UNINITIALIZED] * MAX_CHANNELS for k, n in enumerate(nodes) if n.op == "schmitt"}
    remaining = {k: [0.0] * MAX_CHANNELS for k, n in enumerate(nodes) if n.op == "pulse"}
    delayed = {k: [f32(n.args[0])] for k, n in enumerate(nodes) if n.op == "delay"}
    result = Result(
        {int(n.args[0]): [] for n in nodes if n.op == "output"}, {int(n.args[0]): [] for n in nodes if n.op == "light"}
    )
    rate = f32(sample_rate)
    signals: list[list[float]] = [[] for _ in nodes]
    for frame in range(frames):
        if rate_changes and frame in rate_changes:
            rate = f32(rate_changes[frame])
        dt = f32(1.0 / rate)
        for name, v in ((controls or {}).get(frame) or {}).items():
            current[names[name]] = f32(v)
        for k, node in enumerate(nodes):
            args = [signals[i] for i in node.inputs]
            op = node.op
            out: list[float] = []
            if op == "input":
                port = int(node.args[0])
                out = [f32(v) for v in inputs[port][frame]] if port in inputs else []
            elif op == "param":
                p = params.get(int(node.args[0]), 0.0)
                out = [f32(p if isinstance(p, int | float) else p[frame])]
            elif op == "constant":
                out = [f32(node.args[0])]
            elif op == "control":
                out = [current[k]]
            elif op == "sample_rate":
                out = [rate]
            elif op == "sample_time":
                out = [dt]
            elif op in _ELEMENTWISE:
                out = _elementwise(op, args)
            elif op == "phasor":
                n = max(1, len(args[0]))
                freq, phase = _lanes(args[0], n), phases[k]
                for c in range(n):
                    phase[c] = f32(phase[c] + f32(freq[c] * dt))
                    if phase[c] >= 1.0:
                        phase[c] = f32(phase[c] - 1.0)
                out = phase[:n]
            elif op == "schmitt":
                n = max(1, len(args[0]))
                x, state = _lanes(args[0], n), triggers[k]
                low, high = f32(node.args[0]), f32(node.args[1])
                for c in range(n):
                    fired = 0.0
                    if state[c] == _LOW and x[c] >= high:
                        state[c], fired = _HIGH, 1.0
                    elif state[c] == _HIGH and x[c] <= low:
                        state[c] = _LOW
                    elif state[c] == _UNINITIALIZED and x[c] >= high:
                        state[c] = _HIGH
                    elif state[c] == _UNINITIALIZED and x[c] <= low:
                        state[c] = _LOW
                    out.append(fired)
            elif op == "pulse":
                n = max(1, len(args[0]))
                trigger, left = _lanes(args[0], n), remaining[k]
                duration = f32(node.args[0])
                for c in range(n):
                    if trigger[c] > 0 and duration > left[c]:
                        left[c] = duration
                    high = left[c] > 0
                    if high:
                        left[c] = f32(left[c] - dt)
                    out.append(1.0 if high else 0.0)
            elif op == "delay":
                out = list(delayed[k])
            elif op == "output":
                result.outputs[int(node.args[0])].append(_lanes(args[0], max(1, len(args[0]))))
            elif op == "light":
                result.lights[int(node.args[0])].append(args[0][0] if args[0] else 0.0)
            else:  # pragma: no cover - Plan only holds registered ops
                raise GraphError(f"the reference does not know node type {op!r}")
            signals[k] = out
        for target, source in plan.latches:
            delayed[target] = list(signals[source])
    return result
