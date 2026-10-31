# SPDX-License-Identifier: GPL-3.0-or-later
"""The compiled tier for DSP graphs (RP-21C): a graph as C++ for a kernel library.

:func:`emit` writes one C++ source file that defines a kernel type for each
graph and registers them with ``RACKPY_KERNEL_LIBRARY``. Each kernel
composes RackPy's node types from ``rackpy/graph.hpp`` statically, in the
plan's order, so the compiler sees the whole graph: no dispatch per node, and
constants and channel counts it can propagate. Build the file as any kernel
library (see *Process audio*), list it in ``rackpy-loader.json``, and use the
types like any kernel: ``rackpy.dsp.kernel.load("namespace.Name")()``.

The generated code computes exactly what the plan interpreter computes: the
nodes are the same functions, and each node's result passes a register
barrier (``settle``) so that ``-funsafe-math-optimizations`` cannot
reassociate across nodes. A kernel's fields are the graph's wiring
(``input[0]``, ...), defaulting to the graph's own indices, then its controls.

Native extension nodes are not composed: their types live in other
libraries, out of a generated library's reach. Graphs that use them run on
the interpreter (:meth:`rackpy.dsp.graph.Graph.compile`). Expander nodes are
composed through RackPy's expander services (``rackpy/expanders.hpp``,
RP-23C), which RackPy gives the library when it loads it; graphs with them
always take the polyphonic path.

``bin/rackpy-graph emit`` runs this from the command line. Pure Python.
"""

from __future__ import annotations

import math
from collections.abc import Mapping

from rackpy.dsp.graph import LANE, Graph, GraphError, Plan
from rackpy.dsp.graph_reference import f32

__all__ = ["emit"]

_NS = "rackpy::graph::"
_BINARY = {
    op: _NS + fn
    for op, fn in {
        "add": "add",
        "sub": "sub",
        "mul": "mul",
        "div": "div",
        "min": "fmin",
        "max": "fmax",
        "lt": "lt",
        "le": "le",
        "gt": "gt",
        "ge": "ge",
    }.items()
}
_UNARY = {
    op: _NS + fn for op, fn in {"neg": "neg", "abs": "fabs", "pitch_to_freq": "pitchToFreq", "sin2pi": "sin2pi"}.items()
}
_TERNARY = {"clamp": _NS + "clamp", "where": _NS + "where"}
_PORTS = {"input": "INPUT", "param": "PARAM", "output": "OUTPUT", "light": "LIGHT"}
_STATE = {"phasor": "Phasor", "schmitt": "Schmitt", "pulse": "Pulse", "delay": "Delay"}
_EXPANDER = ("expander_send", "expander_receive")
_IO = ("audio_input", "audio_output", "midi_cv")
_SINKS = ("output", "light", "expander_send", "audio_output")


def _float(x: float) -> str:
    """An exact C++ float literal for x as float32 (hex), with the decimal value as a comment."""
    value = f32(x)
    return f"{value.hex()}f /* {value!r} */"


def _string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _identifier(name: str, what: str) -> None:
    if not name.isidentifier() or not name.isascii():
        raise GraphError(f"{what} must be an ASCII identifier, not {name!r}")


def _kernel(name: str, namespace: str, plan: Plan) -> tuple[str, str]:
    """The kernel struct for one plan, and its registration."""
    nodes = plan.nodes
    exchanges = any(node.op in _EXPANDER for node in nodes)
    uses_io = any(node.op in _IO for node in nodes)
    for node in nodes:
        if node.op in ("audio_input", "audio_output") and (len(node.args) != 1 or node.args[0] not in range(16)):
            raise GraphError(f"{node.op} needs an audio channel from 0 to 15")
        if node.op == "midi_cv":
            limits = ((0, 3), (0, 11), (1, 16), (0, 3), (0, 3), None, (0, 1), (1, 1 << 20), (0, 1), (0, 1))
            if len(node.args) != len(limits) or any(
                not math.isfinite(x)
                or (limit is not None and (not float(x).is_integer() or not limit[0] <= x <= limit[1]))
                for x, limit in zip(node.args, limits, strict=True)
            ):
                raise GraphError("midi_cv has an invalid setting")
    if any("." in node.op for node in nodes):
        extensions = sorted({node.op for node in nodes if "." in node.op})
        raise GraphError(
            f"graph {name} uses native extension nodes ({', '.join(extensions)}), which the compiled tier cannot "
            "compose; run it with Graph.compile() instead"
        )
    # Fields: each port, param and light the plan uses, in first use, then the controls (as the interpreter's type).
    fields: list[tuple[str, str, int]] = []  # (python name, kind, index)
    field_of: dict[tuple[str, int], int] = {}
    for node in nodes:
        if node.op in _PORTS:
            key = (node.op, int(node.args[0]))
            if key not in field_of:
                field_of[key] = len(fields)
                fields.append((f"{node.op}[{key[1]}]", _PORTS[node.op], key[1]))
    control_slot = {c.node: slot for slot, c in enumerate(plan.controls)}

    def field(node_index: int) -> str:
        node = nodes[node_index]
        return f"f{field_of[(node.op, int(node.args[0]))]}"

    members: list[str] = []
    init: list[str] = []
    reset: list[str] = []
    if uses_io:
        members.append("\trackpy::io::Link ioLink;")
    if exchanges:
        # RackPy's expander services (rackpy/expanders.hpp), given to the library when RackPy loads it.
        members.append("\tconst rackpy::expander::Schema* schema;  // rackpy.graph")
        message = _string(f"{namespace}.{name}: RackPy gave this library no expander services")
        init.append(
            f'\t\tschema = rackpy::expander::schema("rackpy.graph");\n\t\tif (!schema)\n'
            f"\t\t\tthrow std::invalid_argument({message});"
        )
    for k, (label, _kind, _index) in enumerate(fields):
        members.append(f"\tint f{k};  // {label}")
        message = _string(f"{namespace}.{name}: {label} must be wired")
        init.append(f"\t\tf{k} = (int) v[{k}];\n\t\tif (f{k} < 0)\n\t\t\tthrow std::invalid_argument({message});")
    for slot, control in enumerate(plan.controls):
        members.append(f"\tfloat k{slot};  // control {control.name}")
        init.append(f"\t\tk{slot} = (float) v[{len(fields) + slot}];")
    for k, node in enumerate(nodes):
        if node.op in _STATE:
            members.append(f"\t{_STATE[node.op]} n{k};")
            reset.append(f"\t\tn{k}.reset();")
            if node.op == "schmitt":
                init.append(f"\t\tn{k}.low = {_float(node.args[0])};\n\t\tn{k}.high = {_float(node.args[1])};")
            elif node.op == "pulse":
                init.append(f"\t\tn{k}.duration = {_float(node.args[0])};")
            elif node.op == "delay":
                init.append(f"\t\tn{k}.initial = {_float(node.args[0])};")
        if node.op == "midi_cv":
            members.append(f"\tMidiCV n{k};")
            values = node.args
            init.append(
                f"\t\tn{k}.input = {int(values[0])}; n{k}.output = (MidiCvOutput) {int(values[1])};\n"
                f"\t\tn{k}.configure({int(values[2])}, {int(values[3])}, {int(values[4])}, {_float(values[5])}, "
                f"{int(values[6])}, {int(values[7])}, {int(values[8])}, {int(values[9])});"
            )
            reset.append(f"\t\tn{k}.reset();")
        if node.op in _EXPANDER:
            side, lane = int(node.args[0]), int(node.args[1])
            members.append(f"\trackpy::expander::Link l{k};  // {node.op}: side {side}, lane {lane}")
            if node.op == "expander_receive":
                members.append(f"\trackpy::expander::Link o{k};")
        if node.op not in _SINKS:
            members.append(f"\tSignal p{k};  // {node.op}, on the polyphonic path")

    mono: list[str] = []
    poly: list[str] = []
    for k, node in enumerate(nodes):
        op, i = node.op, node.inputs
        # The single-channel path: a value and a channel count (0 or 1) per node; a value is 0 without a channel.
        c, v = f"c{k}", f"v{k}"
        if op == "input":
            port = f"m.inputs[{field(k)}]"
            mono.append(f"int {c} = {port}.getChannels(); float {v} = {c} ? {port}.getVoltage(0) : 0.f;")
        elif op == "param":
            mono.append(f"int {c} = 1; float {v} = m.params[{field(k)}].getValue();")
        elif op == "constant":
            mono.append(f"int {c} = 1; float {v} = {_float(node.args[0])};")
        elif op == "control":
            mono.append(f"int {c} = 1; float {v} = k{control_slot[k]};")
        elif op == "sample_rate":
            mono.append(f"int {c} = 1; float {v} = args.sampleRate;")
        elif op == "sample_time":
            mono.append(f"int {c} = 1; float {v} = args.sampleTime;")
        elif op in _BINARY:
            mono.append(
                f"int {c} = std::max(c{i[0]}, c{i[1]}); float {v} = {c} ? {_BINARY[op]}(v{i[0]}, v{i[1]}) : 0.f;"
            )
        elif op in _UNARY:
            mono.append(f"int {c} = c{i[0]}; float {v} = {c} ? {_UNARY[op]}(v{i[0]}) : 0.f;")
        elif op in _TERNARY:
            most = f"std::max(c{i[0]}, std::max(c{i[1]}, c{i[2]}))"
            mono.append(f"int {c} = {most}; float {v} = {c} ? {_TERNARY[op]}(v{i[0]}, v{i[1]}, v{i[2]}) : 0.f;")
        elif op == "phasor":
            mono.append(f"int {c} = 1; float {v} = n{k}.step(0, v{i[0]}, args.sampleTime);")
        elif op == "schmitt":
            mono.append(f"int {c} = 1; float {v} = n{k}.step(0, v{i[0]});")
        elif op == "pulse":
            mono.append(f"int {c} = 1; float {v} = n{k}.step(0, v{i[0]}, args.sampleTime);")
        elif op == "delay":
            mono.append(f"int {c} = n{k}.held.channels; float {v} = {c} ? n{k}.held.v[0] : 0.f;")
        elif op == "output":
            port = f"m.outputs[{field(k)}]"
            mono.append(f"{port}.setChannels(1); {port}.setVoltage(v{i[0]});")
        elif op == "light":
            mono.append(f"m.lights[{field(k)}].setBrightness(v{i[0]});")
        if op not in (*_EXPANDER, *_IO):  # these graphs take the polyphonic path only
            if op not in ("output", "light"):
                mono[-1] = f"[[maybe_unused]] {mono[-1]} settle({v});"
            mono[-1] += f"  // {k}: {op}"
        # The polyphonic path: the node types' Signal functions, as the interpreter calls them.
        out = f"p{k}"
        a = [f"p{x}" for x in i]
        if op == "input":
            line = f"readInput({out}, m.inputs[{field(k)}]);"
        elif op == "param":
            line = f"mono({out}, m.params[{field(k)}].getValue());"
        elif op == "constant":
            line = f"mono({out}, {_float(node.args[0])});"
        elif op == "control":
            line = f"mono({out}, k{control_slot[k]});"
        elif op == "sample_rate":
            line = f"mono({out}, args.sampleRate);"
        elif op == "sample_time":
            line = f"mono({out}, args.sampleTime);"
        elif op == "audio_input":
            line = f"mono({out}, audioInput(ioFrame, {int(node.args[0])}));"
        elif op == "audio_output":
            line = f"audioOutput(ioFrame, {int(node.args[0])}, {a[0]});"
        elif op == "midi_cv":
            line = f"n{k}.process({out}, ioFrame, args.sampleTime);"
        elif op in _BINARY:
            line = f"map2({out}, {a[0]}, {a[1]}, [](float x, float y) {{ return {_BINARY[op]}(x, y); }});"
        elif op in _UNARY:
            line = f"map1({out}, {a[0]}, [](float x) {{ return {_UNARY[op]}(x); }});"
        elif op in _TERNARY:
            fn = f"[](float x, float y, float z) {{ return {_TERNARY[op]}(x, y, z); }}"
            line = f"map3({out}, {a[0]}, {a[1]}, {a[2]}, {fn});"
        elif op in ("phasor", "pulse"):
            line = f"n{k}.process({out}, {a[0]}, args.sampleTime);"
        elif op == "schmitt":
            line = f"n{k}.process({out}, {a[0]});"
        elif op == "delay":
            line = f"n{k}.process({out});"
        elif op == "output":
            line = f"writeOutput(m.outputs[{field(k)}], {a[0]});"
        elif op == "light":
            line = f"writeLight(m.lights[{field(k)}], {a[0]});"
        elif op in _EXPANDER:
            side, lane = int(node.args[0]), int(node.args[1])
            end, offset = (lane + 1) * LANE, lane * LANE
            if op == "expander_send":
                line = (
                    f"if (float* e = rackpy::expander::send(m, {side}, schema, {end}, l{k})) "
                    f"writeLane(e + {offset}, {a[0]});"
                )
            else:
                line = (
                    f"if (float* e = rackpy::expander::receive(m, {side}, schema, {end}, args.frame, l{k}, o{k})) "
                    f"readLane({out}, e + {offset}); else {out}.channels = 0;"
                )
        else:  # pragma: no cover - Plan only holds registered ops
            raise GraphError(f"the compiled tier does not know node type {op!r}")
        if op not in _SINKS:
            line += f" settle({out});"
        poly.append(f"{line}  // {k}: {op}")
    for target, source in plan.latches:
        mono.append(f"n{target}.held.channels = c{source}; n{target}.held.v[0] = v{source};  // latch {target}")
        poly.append(f"n{target}.latch(p{source});  // latch {target}")

    narrow = [f"m.inputs[f{field_of[key]}].getChannels() <= 1" for key in field_of if key[0] == "input"]
    narrow += [f"n{target}.held.channels <= 1" for target, _ in plan.latches]
    condition = " && ".join(narrow) or "true"
    indent = "\n\t\t\t"
    if exchanges or uses_io:
        # Received lanes and MIDI can be polyphonic; I/O graphs use the same path for all nodes.
        body = "\t\t" + "\n\t\t".join(poly)
        if uses_io:
            body = "\t\tconst rackpy::io::Frame* ioFrame = rackpy::io::frame(m, ioLink);\n" + body
    else:
        body = f"""		// Every input and delay holds at most one channel, so every signal does: one value per node.
		if ({condition}) {{
			{indent.join(mono)}
		}}
		else {{
			{indent.join(poly)}
		}}"""
    setters = "".join(f"\t\t\tcase {slot}: k{slot} = (float) value; break;\n" for slot in range(len(plan.controls)))

    struct = f"""/** Graph {name}: {len(nodes)} nodes, composed statically. */
struct {name} final : rackpy::Kernel {{
{chr(10).join(members)}

	explicit {name}(const std::vector<double>& v) {{
		(void) v;
{chr(10).join(init)}
		reset();
	}}

	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {{
		(void) args;
{body}
		return true;
	}}

	void reset() noexcept override {{
{chr(10).join(reset)}
	}}

	void setControl(int slot, double value) noexcept override {{
		switch (slot) {{
{setters}			default: break;
		}}
	}}
}};
"""
    field_entries = [
        f"\t\t\t{{{_string(label)}, rackpy::Field::{kind}, {index}, false, "
        f"{_string('wired by the graph (defaults to its own index)')}}},"
        for label, kind, index in fields
    ] + [
        f"\t\t\t{{{_string(c.name)}, rackpy::Field::NUMBER, {f32(c.default)!r}, false, {_string('a control')}}},"
        for c in plan.controls
    ]
    controls = ", ".join(_string(c.name) for c in plan.controls)
    registration = f"""	registrar.add(rackpy::KernelType{{
		{_string(name)},
		{_string(f"Graph {name}, compiled by bin/rackpy-graph: {len(nodes)} nodes.")},
		{{
{chr(10).join(field_entries)}
		}},
		{{{controls}}},
		[](const std::vector<double>& v) {{ return std::make_unique<{name}>(v); }},
	}});"""
    return struct, registration


def emit(graphs: Mapping[str, Graph | Plan], namespace: str, source: str = "") -> str:
    """C++ for a kernel library that registers each graph as ``namespace.Name``.

    Raises :class:`~rackpy.dsp.graph.GraphError` for what cannot be compiled.
    """
    _identifier(namespace, "the library's namespace")
    if namespace == "rackpy" or namespace.startswith("rackpy_"):
        raise GraphError(f"the namespace {namespace} is reserved for RackPy")
    if not graphs:
        raise GraphError("there are no graphs to compile")
    structs: list[str] = []
    registrations: list[str] = []
    exchanges = False
    uses_io = False
    for name, graph in graphs.items():
        _identifier(name, "a graph's kernel type name")
        plan = graph.plan() if isinstance(graph, Graph) else graph
        struct, registration = _kernel(name, namespace, plan)
        structs.append(struct)
        registrations.append(registration)
        exchanges = exchanges or any(node.op in _EXPANDER for node in plan.nodes)
        uses_io = uses_io or any(node.op in _IO for node in plan.nodes)
    io_include = "#include <rackpy/io.hpp>\n" if uses_io else ""
    io_entry = "\n// RackPy's audio and MIDI services.\nRACKPY_IO\n" if uses_io else ""
    expanders_include = "#include <rackpy/expanders.hpp>\n" if exchanges else ""
    expanders_entry = (
        "\n// RackPy's expander services, for the graphs that exchange with neighbours (RP-23C).\nRACKPY_EXPANDERS\n"
        if exchanges
        else ""
    )
    origin = f" from {source}" if source else ""
    return f"""// Generated by bin/rackpy-graph{origin}; do not edit.
// The compiled tier of RackPy's DSP graphs (RP-21C): each kernel composes the
// node types of rackpy/graph.hpp statically, as the plan interpreter runs them.
#include <rack.hpp>
{expanders_include}{io_include}#include <rackpy/graph.hpp>
#include <rackpy/kernel.hpp>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {{

using namespace rackpy::graph;

{chr(10).join(structs)}
}}  // namespace

RACKPY_KERNEL_LIBRARY({_string(namespace)}, registrar) {{
{chr(10).join(registrations)}
}}
{expanders_entry}{io_entry}"""
