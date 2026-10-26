# SPDX-License-Identifier: GPL-3.0-or-later
"""DSP graphs: a module's per-sample processing, described in Python (RP-21).

A :class:`Graph` is built from native node types: ports, parameters and
controls in, arithmetic, oscillators and Rack's own triggers in between,
outputs and lights out. Python only describes the graph; it never runs per
sample. Arbitrary Python functions are not compiled: an expression on
signals, such as ``a * b + 1``, builds nodes, and anything else (``if
signal:``, ``math.sin(signal)``) is refused.

Every edge carries a polyphonic signal of 0 to 16 channels per frame, as
Rack's cables do. Inputs carry their cable's channels; a mono operand
broadcasts across the other's channels, and channels missing from a shorter
operand read 0 V. Feedback within the module needs an explicit
:meth:`Graph.delay` of one sample: a cycle without one is refused, with its
path, rather than given hidden latency.

A graph may also exchange signals with the modules beside it, through
Rack's expanders: :meth:`Graph.expander_send` writes a signal into a lane of
the neighbour's ``"rackpy.graph"`` message, and :meth:`Graph.expander_receive`
reads what the neighbour sent, exactly one frame later, as Rack's double
buffers deliver it (RP-23B).

:meth:`Graph.plan` validates the graph and lowers it to a :class:`Plan`,
the description its execution tiers share. :meth:`Graph.compile` turns it
into a native kernel run by the plan interpreter on Rack's engine thread,
for ``Module.set_kernel``; :mod:`rackpy.dsp.graph_reference` evaluates it
offline, for tests and teaching; :mod:`rackpy.dsp.graph_codegen` emits a
compiled tier that composes the nodes statically.

This module is pure Python and works outside Rack, except for
:meth:`Graph.compile`.
"""

from __future__ import annotations

import dataclasses
import math
import sys
from typing import TYPE_CHECKING, Any, Final

if TYPE_CHECKING:
    from _rackpy_host.dsp import Kernel

__all__ = [
    "EXPANDER_SCHEMA",
    "LANE",
    "MAX_CHANNELS",
    "MAX_LANES",
    "OPS",
    "Control",
    "Delay",
    "Forward",
    "Graph",
    "GraphError",
    "Node",
    "NodeType",
    "Op",
    "Plan",
    "Signal",
    "node_types",
]

MAX_CHANNELS: Final = 16
"""Rack's ``PORT_MAX_CHANNELS``: the most channels a signal carries."""

EXPANDER_SCHEMA: Final = "rackpy.graph"
"""The schema of the expander messages graphs exchange (:meth:`Graph.expander_send`)."""

LANE: Final = 1 + MAX_CHANNELS
"""Values in one lane of a ``"rackpy.graph"`` message: the channel count, then the voltages."""

MAX_LANES: Final = 64
"""The most lanes a graph addresses on one side."""


class GraphError(ValueError):
    """A graph that cannot run: the message says which node, where it was made, and what to change."""


@dataclasses.dataclass(frozen=True)
class Op:
    """A native node type: how many signals it reads, which numbers configure it, and whether it makes a signal."""

    name: str
    inputs: int
    args: tuple[str, ...]
    signal: bool
    doc: str


def _ops(*ops: Op) -> dict[str, Op]:
    return {op.name: op for op in ops}


OPS: Final[dict[str, Op]] = _ops(
    Op("input", 0, ("port",), True, "An input port's voltages; its channels are the cable's (0 when unpatched)."),
    Op("param", 0, ("index",), True, "A parameter's value, mono."),
    Op("constant", 0, ("value",), True, "A fixed number, mono."),
    Op("control", 0, ("default",), True, "A named number, mono, that Python may change while the plan runs."),
    Op("sample_rate", 0, (), True, "The engine's sample rate in Hz, mono."),
    Op("sample_time", 0, (), True, "The duration of one frame in seconds, mono."),
    Op("add", 2, (), True, "a + b"),
    Op("sub", 2, (), True, "a - b"),
    Op("mul", 2, (), True, "a * b"),
    Op("div", 2, (), True, "a / b (IEEE: division by zero gives an infinity or NaN)"),
    Op("min", 2, (), True, "std::fmin(a, b)"),
    Op("max", 2, (), True, "std::fmax(a, b)"),
    Op("lt", 2, (), True, "1 where a < b, else 0"),
    Op("le", 2, (), True, "1 where a <= b, else 0"),
    Op("gt", 2, (), True, "1 where a > b, else 0"),
    Op("ge", 2, (), True, "1 where a >= b, else 0"),
    Op("neg", 1, (), True, "-x"),
    Op("abs", 1, (), True, "std::fabs(x)"),
    Op("clamp", 3, (), True, "rack::math::clamp(x, low, high): std::fmax(std::fmin(x, high), low)"),
    Op("where", 3, (), True, "b where the condition is above 0, else c"),
    Op("pitch_to_freq", 1, (), True, "dsp::FREQ_C4 * std::pow(2.f, v): V/oct to Hz, 0 V is C4"),
    Op("sin2pi", 1, (), True, "std::sin(2.f * float(M_PI) * x)"),
    Op(
        "phasor",
        1,
        (),
        True,
        "A phase from 0 to 1 per channel (at least one): phase += freq * sampleTime, less 1 once it reaches 1; "
        "the new phase.",
    ),
    Op(
        "schmitt",
        1,
        ("low", "high"),
        True,
        "Rack's dsp::SchmittTrigger per channel (at least one): 1 on the frame the input rises to high, else 0.",
    ),
    Op(
        "pulse",
        1,
        ("duration",),
        True,
        "Rack's dsp::PulseGenerator per channel (at least one): a trigger above 0 starts a pulse of duration "
        "seconds; 1 while it is high, else 0.",
    ),
    Op(
        "delay",
        0,
        ("initial",),
        True,
        "The signal it is fed, one frame later (its channels too); one mono channel of initial at the start.",
    ),
    Op("output", 1, ("port",), False, "Writes an output port: its channels (at least one) and voltages."),
    Op("light", 1, ("index",), False, "Sets a light's brightness from channel 0."),
    Op(
        "expander_send",
        1,
        ("side", "lane"),
        False,
        "Writes the signal into lane `lane` of the neighbour's message on `side` (0 left, 1 right) and requests "
        "the flip, if the neighbour is a RackPy module whose facing expander points back and holds "
        '"rackpy.graph" messages with that lane; otherwise nothing.',
    ),
    Op(
        "expander_receive",
        0,
        ("side", "lane"),
        True,
        "What the neighbour on `side` sent in lane `lane` during the previous frame, read once; 0 channels if "
        'nothing was sent, the neighbour is not a RackPy module, or this module holds no "rackpy.graph" '
        "messages with that lane on that side.",
    ),
)


@dataclasses.dataclass(frozen=True)
class Node:
    """One node of a plan: ``op`` reads the signals of the earlier nodes ``inputs``, configured by ``args``.

    For a native extension node (``op`` is ``"namespace.Name"``), ``names``
    names each of ``args``; for RackPy's own node types it is empty.
    """

    op: str
    inputs: tuple[int, ...]
    args: tuple[float, ...]
    names: tuple[str, ...] = ()


@dataclasses.dataclass(frozen=True)
class NodeType:
    """A native extension node type from a kernel library: see :func:`node_types`."""

    name: str
    doc: str
    inputs: int
    args: tuple[str, ...]


def node_types() -> tuple[NodeType, ...]:
    """The native extension node types that kernel libraries registered. Available inside Rack only."""
    try:
        from _rackpy_host import dsp
    except ImportError as e:
        raise ImportError("node_types() needs Rack, in a RackPy plugin") from e
    return tuple(NodeType(name, doc, inputs, tuple(args)) for name, doc, inputs, args in dsp.graph_node_types())


@dataclasses.dataclass(frozen=True)
class Control:
    """A named control: the ``control`` node it sets, and its starting value."""

    name: str
    node: int
    default: float


@dataclasses.dataclass(frozen=True)
class Plan:
    """A validated graph, lowered for execution.

    ``nodes`` run in order once per frame; each reads only earlier nodes'
    signals, except that a ``delay`` node gives the signal ``latches`` feeds
    it, as of the end of the previous frame. After every node has run, each
    ``(delay, source)`` latch stores the source's signal for the next frame.
    """

    nodes: tuple[Node, ...]
    latches: tuple[tuple[int, int], ...]
    controls: tuple[Control, ...]

    def to_dict(self) -> dict[str, list[Any]]:
        """The plan as plain data (lists, numbers, strings)."""
        return {
            "nodes": [
                {"op": n.op, "inputs": list(n.inputs), "args": list(n.args)}
                | ({"names": list(n.names)} if n.names else {})
                for n in self.nodes
            ],
            "latches": [list(latch) for latch in self.latches],
            "controls": [{"name": c.name, "node": c.node, "default": c.default} for c in self.controls],
        }


class Signal:
    """A per-sample value in a graph: the output of one node. Combine signals with arithmetic and comparisons."""

    __slots__ = ("_graph", "_id")

    def __init__(self, graph: Graph, node_id: int) -> None:
        self._graph = graph
        self._id = node_id

    @property
    def graph(self) -> Graph:
        return self._graph

    def __add__(self, other: Signal | float) -> Signal:
        return self._graph._apply("add", self, other)

    def __radd__(self, other: Signal | float) -> Signal:
        return self._graph._apply("add", other, self)

    def __sub__(self, other: Signal | float) -> Signal:
        return self._graph._apply("sub", self, other)

    def __rsub__(self, other: Signal | float) -> Signal:
        return self._graph._apply("sub", other, self)

    def __mul__(self, other: Signal | float) -> Signal:
        return self._graph._apply("mul", self, other)

    def __rmul__(self, other: Signal | float) -> Signal:
        return self._graph._apply("mul", other, self)

    def __truediv__(self, other: Signal | float) -> Signal:
        return self._graph._apply("div", self, other)

    def __rtruediv__(self, other: Signal | float) -> Signal:
        return self._graph._apply("div", other, self)

    def __neg__(self) -> Signal:
        return self._graph._apply("neg", self)

    def __pos__(self) -> Signal:
        return self

    def __abs__(self) -> Signal:
        return self._graph._apply("abs", self)

    def __lt__(self, other: Signal | float) -> Signal:
        return self._graph._apply("lt", self, other)

    def __le__(self, other: Signal | float) -> Signal:
        return self._graph._apply("le", self, other)

    def __gt__(self, other: Signal | float) -> Signal:
        return self._graph._apply("gt", self, other)

    def __ge__(self, other: Signal | float) -> Signal:
        return self._graph._apply("ge", self, other)

    def clamp(self, low: Signal | float = 0.0, high: Signal | float = 1.0) -> Signal:
        """``rack::math::clamp(self, low, high)``."""
        return self._graph._apply("clamp", self, low, high)

    def __bool__(self) -> bool:
        raise TypeError(
            "a Signal has a value per sample, computed natively; Python cannot branch on it. "
            "Use Graph.where(condition, a, b) to choose per sample."
        )

    def __float__(self) -> float:
        raise TypeError(
            "a Signal has a value per sample, computed natively; it is not a Python number. "
            "Arbitrary Python functions are not compiled: build the computation from Graph's nodes."
        )

    def __index__(self) -> int:
        raise TypeError("a Signal is not a Python number; ports, params and lights take whole numbers")

    def __repr__(self) -> str:
        return f"<Signal {self._graph._describe(self._id)}>"


class Delay(Signal):
    """A one-sample delay: the signal :meth:`Delay.feed` gives it, one frame later. The only way to close a cycle."""

    __slots__ = ()

    def feed(self, source: Signal | float) -> None:
        """Sets what the delay outputs on the next frame; once."""
        self._graph._feed(self, source)


class Forward(Signal):
    """A signal named before it is defined; :meth:`Forward.bind` defines it, once. Adds no delay."""

    __slots__ = ()

    def bind(self, source: Signal | float) -> None:
        self._graph._bind(self, source)


@dataclasses.dataclass
class _Made:
    op: str
    inputs: list[int]
    args: tuple[float, ...]
    where: str
    name: str | None = None  # a control's name
    names: tuple[str, ...] = ()  # an extension node's argument names


def _caller() -> str:
    """file:line of the first frame outside this module: where the user made the node."""
    frame = sys._getframe(1)
    while frame.f_back is not None and frame.f_globals.get("__name__") == __name__:
        frame = frame.f_back
    return f"{frame.f_code.co_filename}:{frame.f_lineno}"


def _number(value: object, what: str) -> float:
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise GraphError(f"{what} must be a number, not {value!r}")
    number = float(value)
    if not math.isfinite(number):
        raise GraphError(f"{what} must be finite, not {number!r}")
    return number


def _index(value: object, what: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise GraphError(f"{what} must be a whole number, not {value!r}")
    if value < 0:
        raise GraphError(f"{what} must not be negative, not {value}")
    return value


class Graph:
    """A module's per-sample processing, built from native nodes. See the module documentation."""

    def __init__(self) -> None:
        self._made: list[_Made] = []
        self._fed: dict[int, int] = {}  # delay -> source
        self._bound: dict[int, int] = {}  # forward -> source
        self._outputs: dict[int, int] = {}  # port -> output node
        self._lights: dict[int, int] = {}  # index -> light node
        self._sends: dict[tuple[int, int], int] = {}  # (side, lane) -> expander_send node
        self._receives: dict[tuple[int, int], int] = {}  # (side, lane) -> expander_receive node
        self._controls: dict[str, int] = {}

    # --- sources ---------------------------------------------------------------------------
    def input(self, port: int) -> Signal:
        """An input port's voltages, with the cable's channels (0 when unpatched)."""
        return self._signal("input", (), (_index(port, "input port"),))

    def param(self, index: int) -> Signal:
        """A parameter's value."""
        return self._signal("param", (), (_index(index, "param index"),))

    def constant(self, value: float) -> Signal:
        return self._signal("constant", (), (_number(value, "a constant"),))

    def control(self, name: str, default: float = 0.0) -> Signal:
        """A number Python may change while the plan runs, by name."""
        if not isinstance(name, str) or not name.isidentifier():
            raise GraphError(f"a control's name must be an identifier, not {name!r}")
        if name in self._controls:
            first = self._made[self._controls[name]].where
            raise GraphError(f"control {name!r} is already defined (at {first}); names are unique")
        signal = self._signal("control", (), (_number(default, f"control {name!r}'s default"),))
        self._made[signal._id].name = name
        self._controls[name] = signal._id
        return signal

    def sample_rate(self) -> Signal:
        return self._signal("sample_rate", (), ())

    def sample_time(self) -> Signal:
        return self._signal("sample_time", (), ())

    # --- functions ---------------------------------------------------------------------------
    def minimum(self, a: Signal | float, b: Signal | float) -> Signal:
        """``std::fmin(a, b)`` per channel."""
        return self._apply("min", a, b)

    def maximum(self, a: Signal | float, b: Signal | float) -> Signal:
        """``std::fmax(a, b)`` per channel."""
        return self._apply("max", a, b)

    def clamp(self, x: Signal | float, low: Signal | float = 0.0, high: Signal | float = 1.0) -> Signal:
        """``rack::math::clamp(x, low, high)`` per channel."""
        return self._apply("clamp", x, low, high)

    def where(self, condition: Signal | float, a: Signal | float, b: Signal | float) -> Signal:
        """``a`` where ``condition`` is above 0, else ``b``, per channel."""
        return self._apply("where", condition, a, b)

    def pitch_to_freq(self, pitch: Signal | float) -> Signal:
        """V/oct to Hz, 0 V being C4: ``dsp::FREQ_C4 * std::pow(2.f, pitch)``."""
        return self._apply("pitch_to_freq", pitch)

    def sin2pi(self, phase: Signal | float) -> Signal:
        """``std::sin(2.f * float(M_PI) * phase)``."""
        return self._apply("sin2pi", phase)

    def phasor(self, freq: Signal | float) -> Signal:
        """A phase from 0 to 1 per channel at ``freq`` Hz, as Rack's tutorial oscillator keeps it."""
        return self._apply("phasor", freq)

    def schmitt(self, x: Signal | float, low: float = 0.0, high: float = 1.0) -> Signal:
        """1 on the frame ``x`` rises to ``high`` (from at most ``low``), per channel: Rack's ``SchmittTrigger``."""
        lo, hi = _number(low, "schmitt's low threshold"), _number(high, "schmitt's high threshold")
        return self._apply("schmitt", x, args=(lo, hi))

    def pulse(self, trigger: Signal | float, duration: float = 1e-3) -> Signal:
        """A gate of ``duration`` seconds started by each frame ``trigger`` is above 0: Rack's ``PulseGenerator``."""
        seconds = _number(duration, "pulse's duration")
        if seconds < 0:
            raise GraphError(f"pulse's duration must not be negative, not {seconds}")
        return self._apply("pulse", trigger, args=(seconds,))

    def node(self, name: str, *inputs: Signal | float, **args: float) -> Signal:
        """A native extension node from a kernel library, ``"namespace.Name"``: its signals and named numbers.

        The type, its inputs and its numbers are checked when the graph is
        compiled, inside Rack (:func:`node_types` lists the types). The
        offline reference cannot run extension nodes.
        """
        parts = name.split(".") if isinstance(name, str) else []
        if len(parts) != 2 or not all(part.isidentifier() for part in parts):
            raise GraphError(f'a native node type is named "namespace.Name", not {name!r}')
        if len(inputs) > 8:
            raise GraphError(f"{name} reads at most 8 signals, not {len(inputs)}")
        numbers = tuple(_number(value, f"{name}'s {key}") for key, value in args.items())
        ids = [self._operand(x, f"{name}'s operand {k + 1}") for k, x in enumerate(inputs)]
        self._made.append(_Made(name, ids, numbers, _caller(), names=tuple(args)))
        return Signal(self, len(self._made) - 1)

    def expander_receive(self, side: int, lane: int = 0) -> Signal:
        """What the neighbour on ``side`` (0 left, 1 right) sent in ``lane``, one frame later, read once.

        The module must hold ``"rackpy.graph"`` messages with room for the lane
        on that side (:func:`rackpy.engine.expanders.graph_messages`); the
        signal has 0 channels whenever nothing arrived.
        """
        key = self._lane(side, lane, "expander_receive")
        if key in self._receives:
            raise GraphError(
                f"lane {key[1]} of side {key[0]} is already received (at {self._made[self._receives[key]].where}); "
                "a lane carries one signal: receive it once and reuse the signal"
            )
        node = self._node("expander_receive", (), (float(key[0]), float(key[1])))
        self._receives[key] = node
        return Signal(self, node)

    # --- feedback and forward references -----------------------------------------------------
    def delay(self, initial: float = 0.0) -> Delay:
        """A one-sample delay; give it its source with :meth:`Delay.feed`."""
        made = _Made("delay", [], (_number(initial, "a delay's initial value"),), _caller())
        self._made.append(made)
        return Delay(self, len(self._made) - 1)

    def forward(self) -> Forward:
        """A signal to use before defining it with :meth:`Forward.bind`. Adds no delay."""
        self._made.append(_Made("forward", [], (), _caller()))
        return Forward(self, len(self._made) - 1)

    # --- sinks -------------------------------------------------------------------------------
    def output(self, port: int, signal: Signal | float) -> None:
        """Writes ``signal`` to an output port, once per port."""
        index = _index(port, "output port")
        if index in self._outputs:
            raise GraphError(
                f"output port {index} is already written (at {self._made[self._outputs[index]].where}); "
                "add the signals first"
            )
        self._outputs[index] = self._node("output", (signal,), (float(index),))

    def light(self, index: int, brightness: Signal | float) -> None:
        """Sets a light's brightness from channel 0 of ``brightness``, once per light."""
        light = _index(index, "light index")
        if light in self._lights:
            raise GraphError(f"light {light} is already set (at {self._made[self._lights[light]].where})")
        self._lights[light] = self._node("light", (brightness,), (float(light),))

    def expander_send(self, side: int, signal: Signal | float, lane: int = 0) -> None:
        """Sends ``signal`` to the neighbour on ``side`` (0 left, 1 right) in ``lane``, once per lane.

        It arrives one frame later, at the neighbour's ``expander_receive`` on
        the facing side. It is written only if the neighbour is a RackPy
        module, its facing expander points back to this module, and it holds
        ``"rackpy.graph"`` messages with room for the lane; otherwise nothing
        is sent.
        """
        key = self._lane(side, lane, "expander_send")
        if key in self._sends:
            raise GraphError(
                f"lane {key[1]} of side {key[0]} is already sent (at {self._made[self._sends[key]].where}); "
                "a lane carries one signal"
            )
        self._sends[key] = self._node("expander_send", (signal,), (float(key[0]), float(key[1])))

    # --- execution -----------------------------------------------------------------------------
    def compile(self) -> Kernel:
        """The graph as a native kernel, run by the plan interpreter: hand it to ``Module.set_kernel``.

        The plan is checked again natively, and its kernel allocates all its
        state now, on the main thread. Its fields are the ports, params and
        lights the graph uses, which ``set_kernel`` checks against the
        module; its controls change with ``Kernel.set``. Available inside
        Rack only.
        """
        plan = self.plan()
        try:
            from _rackpy_host import dsp
        except ImportError as e:
            raise ImportError(
                "Graph.compile needs Rack, in a RackPy plugin; rackpy.dsp.graph_reference runs graphs offline"
            ) from e
        return dsp.graph_kernel(plan.to_dict())

    # --- lowering ------------------------------------------------------------------------------
    def plan(self) -> Plan:
        """Validates the graph and lowers it: see :class:`Plan`. Raises :class:`GraphError`."""
        sinks = [*self._outputs.values(), *self._lights.values(), *self._sends.values()]
        if not sinks:
            raise GraphError("the graph writes no output, light or expander lane, so it would do nothing")
        resolve = self._resolver()
        # Nodes the sinks need, through forwards and the delays' sources.
        needed: set[int] = set()
        stack = list(sinks)
        while stack:
            node = resolve(stack.pop())
            if node in needed:
                continue
            needed.add(node)
            made = self._made[node]
            stack.extend(made.inputs)
            if made.op == "delay":
                if node not in self._fed:
                    raise GraphError(f"the delay made at {made.where} is never fed: call its feed(signal)")
                stack.append(self._fed[node])
        order = self._order(needed, resolve)
        position = {node: k for k, node in enumerate(order)}
        nodes = tuple(
            Node(
                self._made[node].op,
                tuple(position[resolve(i)] for i in self._made[node].inputs),
                self._made[node].args,
                self._made[node].names,
            )
            for node in order
        )
        latches = tuple(
            (position[node], position[resolve(self._fed[node])]) for node in order if self._made[node].op == "delay"
        )
        controls = tuple(
            Control(name, position[node], self._made[node].args[0])
            for name, node in sorted(self._controls.items())
            if node in needed
        )
        return Plan(nodes, latches, controls)

    # --- internals -----------------------------------------------------------------------------
    def _signal(self, op: str, inputs: tuple[Signal | float, ...], args: tuple[float, ...]) -> Signal:
        return Signal(self, self._node(op, inputs, args))

    def _apply(self, op: str, *operands: Signal | float, args: tuple[float, ...] = ()) -> Signal:
        return self._signal(op, operands, args)

    def _node(self, op: str, inputs: tuple[Signal | float, ...], args: tuple[float, ...]) -> int:
        spec = OPS[op]
        assert len(inputs) == spec.inputs and len(args) == len(spec.args), op
        ids = [self._operand(x, f"{op}'s operand {k + 1}") for k, x in enumerate(inputs)]
        self._made.append(_Made(op, ids, args, _caller()))
        return len(self._made) - 1

    @staticmethod
    def _lane(side: object, lane: object, what: str) -> tuple[int, int]:
        if not isinstance(side, int) or isinstance(side, bool) or side not in (0, 1):
            raise GraphError(f"{what}'s side is 0 (left) or 1 (right), not {side!r}")
        index = _index(lane, f"{what}'s lane")
        if index >= MAX_LANES:
            raise GraphError(f"{what}'s lane is 0 to {MAX_LANES - 1}, not {index}")
        return side, index

    def _operand(self, value: object, what: str) -> int:
        if isinstance(value, Signal):
            if value._graph is not self:
                raise GraphError(f"{what} belongs to another graph; a graph only reads its own signals")
            return value._id
        if isinstance(value, bool) or not isinstance(value, int | float):
            raise GraphError(
                f"{what} must be a Signal or a number, not {type(value).__name__}: arbitrary Python values and "
                "functions are not compiled"
            )
        return self._signal("constant", (), (_number(value, what),))._id

    def _feed(self, delay: Delay, source: Signal | float) -> None:
        if delay._graph is not self:
            raise GraphError("the delay belongs to another graph")
        if delay._id in self._fed:
            raise GraphError(f"the delay made at {self._made[delay._id].where} is already fed; feed it once")
        self._fed[delay._id] = self._operand(source, "a delay's source")

    def _bind(self, forward: Forward, source: Signal | float) -> None:
        if forward._graph is not self:
            raise GraphError("the forward signal belongs to another graph")
        if forward._id in self._bound:
            raise GraphError(f"the forward signal made at {self._made[forward._id].where} is already bound")
        self._bound[forward._id] = self._operand(source, "a forward signal's source")

    def _resolver(self) -> _Resolve:
        return _Resolve(self)

    def _order(self, needed: set[int], resolve: _Resolve) -> list[int]:
        """The needed nodes in a topological order (creation order among ready ones), delays as sources."""
        waiting = {node: {resolve(i) for i in self._made[node].inputs} for node in needed}
        users: dict[int, list[int]] = {node: [] for node in needed}
        for node, deps in waiting.items():
            for dep in deps:
                users[dep].append(node)
        ready = sorted(node for node, deps in waiting.items() if not deps)
        order: list[int] = []
        while ready:
            node = ready.pop(0)
            order.append(node)
            for user in sorted(users[node]):
                waiting[user].discard(node)
                if not waiting[user]:
                    ready.append(user)
            ready.sort()
        if len(order) != len(needed):
            raise GraphError(self._cycle(set(needed) - set(order), resolve))
        return order

    def _cycle(self, stuck: set[int], resolve: _Resolve) -> str:
        """A message naming one cycle among the stuck nodes."""
        start = min(stuck)
        path = [start]
        seen = {start: 0}
        node = start
        while True:
            node = next(resolve(i) for i in self._made[node].inputs if resolve(i) in stuck)
            if node in seen:
                cycle = [*path[seen[node] :], node]
                break
            seen[node] = len(path)
            path.append(node)
        steps = " <- ".join(self._describe(n) for n in cycle)
        return (
            f"the graph has a cycle with no delay: {steps}. Rack processes one sample at a time, so feedback "
            "within a module needs one sample of delay: make it with Graph.delay() and feed() it"
        )

    def _describe(self, node: int) -> str:
        made = self._made[node]
        label = f"{made.op} {made.name!r}" if made.name else made.op
        return f"{label} (made at {made.where})"


class _Resolve:
    """Follows forward signals to what they are bound to."""

    def __init__(self, graph: Graph) -> None:
        self._graph = graph

    def __call__(self, node: int) -> int:
        seen: list[int] = []
        while self._graph._made[node].op == "forward":
            if node not in self._graph._bound:
                raise GraphError(
                    f"the forward signal made at {self._graph._made[node].where} is never bound: call its bind(signal)"
                )
            if node in seen:
                chain = " <- ".join(self._graph._describe(n) for n in [*seen, node])
                raise GraphError(f"forward signals bound to each other in a loop: {chain}")
            seen.append(node)
            node = self._graph._bound[node]
        return node
