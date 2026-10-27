# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-21C: the compiled tier's code generator (pure Python).

The committed kernel library tests/dsp/kernel_library/graphs.cpp must be what
bin/rackpy-graph writes today from tests/dsp/in_rack_libraries/graphs.py; the
hooks build compiles it, and tests/dsp/in_rack_libraries checks every kernel
in it against the plan interpreter inside Rack.
"""

import runpy
import subprocess
from pathlib import Path

import pytest

from rackpy.dsp.graph import Graph, GraphError
from rackpy.dsp.graph_codegen import emit

pytestmark = pytest.mark.unit
REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "tests/dsp/in_rack_libraries/graphs.py"
GENERATED = REPO / "tests/dsp/kernel_library/graphs.cpp"


def test_the_committed_library_is_current() -> None:
    graphs = runpy.run_path(str(SOURCE))["GRAPHS"]
    expected = emit(graphs, "graphs", source="tests/dsp/in_rack_libraries/graphs.py")
    assert GENERATED.read_text() == expected, "regenerate: bin/rackpy-graph emit " + str(SOURCE.relative_to(REPO))


def test_the_command_line_writes_the_same(tmp_path: Path) -> None:
    out = tmp_path / "graphs.cpp"
    proc = subprocess.run(
        [str(REPO / "bin/rackpy-graph"), "emit", str(SOURCE), "graphs", "--output", str(out)],
        capture_output=True,
        text=True,
        check=False,
        cwd=REPO,
    )
    assert proc.returncode == 0, proc.stderr
    assert out.read_text() == GENERATED.read_text()


def test_each_graph_becomes_a_kernel_type_with_its_wiring_and_controls() -> None:
    text = GENERATED.read_text()
    assert 'RACKPY_KERNEL_LIBRARY("graphs", registrar)' in text
    for name in ("Tutorial", "Everything", "Sine", "SineGain", "Vca"):
        assert f"struct {name} final : rackpy::Kernel" in text and f'"{name}",' in text
    assert '{"level", rackpy::Field::NUMBER, 5.0, false, "a control"}' in text
    assert "settle(" in text and "rackpy::graph::pitchToFreq" in text
    assert "0x1.4000000000000p+2f /* 5.0 */" in text  # exact float literals


def test_what_cannot_be_compiled_is_refused() -> None:
    g = Graph()
    g.output(0, g.input(0))
    for namespace in ("rackpy", "rackpy_extra", "two words", "1st"):
        with pytest.raises(GraphError, match="namespace"):
            emit({"Passthrough": g}, namespace)
    for name in ("lower case", "", "Ok.No"):
        with pytest.raises(GraphError, match="kernel type name"):
            emit({name: g}, "mine")
    with pytest.raises(GraphError, match="no graphs"):
        emit({}, "mine")
    extended = Graph()
    extended.output(0, extended.node("example.Fold", extended.input(0), drive=1.0, limit=5.0))
    with pytest.raises(GraphError, match=r"native extension nodes \(example\.Fold\).*Graph\.compile\(\)"):
        emit({"Folded": extended}, "mine")
    looped = Graph()
    f = looped.forward()
    f.bind(f * 2)
    looped.output(0, f)
    with pytest.raises(GraphError, match="cycle with no delay"):
        emit({"Looped": looped}, "mine")


def test_expander_nodes_compose_through_rackpys_services() -> None:
    """RP-23C: a graph that exchanges takes the polyphonic path and asks RackPy for its services."""
    neighbourly = Graph()
    neighbourly.expander_send(1, neighbourly.input(0), lane=2)
    neighbourly.output(0, neighbourly.expander_receive(0))
    source = emit({"Neighbourly": neighbourly}, "mine")
    assert "#include <rackpy/expanders.hpp>" in source and source.rstrip().endswith("RACKPY_EXPANDERS")
    assert 'rackpy::expander::schema("rackpy.graph")' in source and "no expander services" in source
    assert "rackpy::expander::send(m, 1, schema, 51, l" in source and "writeLane(e + 34, p" in source
    assert "rackpy::expander::receive(m, 0, schema, 17, args.frame, l" in source and "readLane(p" in source
    assert "getChannels() <= 1" not in source  # no single-channel path
    plain = Graph()
    plain.output(0, plain.input(0))
    assert "RACKPY_EXPANDERS" not in emit({"Plain": plain}, "mine")  # only libraries that exchange define it
