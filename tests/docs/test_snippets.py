# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-09: every snippet shown on the site runs where its header says, and prints its expected output.

`runs-in=rack` snippets run inside Rack Free, in the built plugin, as the
trusted entry module's payload; `runs-in=python` snippets run with the
project's interpreter. The page shows the same file (rackpy-example).
"""

import json
import re
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path

import pytest
import rackpy_docs

from tests.docs.conftest import DOCS
from tests.support import assert_clean_exit, linux_x64_only, run_rack, stage_plugin

pytestmark = pytest.mark.docs
SNIPPETS = sorted(rackpy_docs.SNIPPETS.glob("*.py"))
SHOWN = re.compile(r"^```\{rackpy-example\}\s+(\S+)", re.MULTILINE)

# The trusted entry module that runs one snippet inside Rack and reports what it printed.
RUNNER = """
import contextlib, io, json, runpy, traceback
import _rackpy_host

out = io.StringIO()
result = {"ok": True}
try:
    with contextlib.redirect_stdout(out):
        runpy.run_path(SNIPPET, run_name="__main__")
except BaseException:
    result = {"ok": False, "error": traceback.format_exc()}
result["stdout"] = out.getvalue()
_rackpy_host.log_info("snippet-result " + json.dumps(result))
"""


def shown_snippets() -> set[str]:
    return {name for page in DOCS.rglob("*.md") for name in SHOWN.findall(page.read_text())}


def run_in_rack(snippet: Path, bundle: Path, rack: Path, user: Path, modules: tuple[str, ...] = ()) -> str:
    """Run a snippet inside Rack through the plugin's entry module; return its standard output.

    `modules` are the model slugs the snippet adds, which plugin.json must list.
    """
    plugin = stage_plugin(
        bundle,
        user,
        entry="rackpy_snippet",
        files={"pythonpath/rackpy_snippet.py": f"SNIPPET = {str(snippet)!r}\n{RUNNER}"},
    )
    if modules:
        manifest = json.loads((plugin / "plugin.json").read_text())
        manifest["modules"] += [{"slug": m, "name": m, "tags": []} for m in modules]
        (plugin / "plugin.json").write_text(json.dumps(manifest))
    run = run_rack(rack, user, settle=0.3)
    assert_clean_exit(run)
    if modules:
        assert run.lines("Loaded plugin RackPy"), run.tail()  # its models matched plugin.json
    lines = run.lines("snippet-result ")
    assert len(lines) == 1, run.tail()
    result = json.loads(lines[0].split("snippet-result ", 1)[1])
    if not result["ok"]:
        raise AssertionError(f"{snippet.name} raised inside Rack:\n{result['error']}")
    output: str = result["stdout"]
    return output


def run_in_python(snippet: Path) -> str:
    proc = subprocess.run([sys.executable, str(snippet)], capture_output=True, text=True, check=False)
    if proc.returncode:
        raise AssertionError(f"{snippet.name} failed:\n{proc.stderr}")
    return proc.stdout


def test_every_snippet_is_well_formed_and_shown() -> None:
    assert SNIPPETS, "docs/snippets/ has no snippets"
    for path in SNIPPETS:
        rackpy_docs.read_snippet(path)  # raises on a bad header or a missing output file
    names = {p.name for p in SNIPPETS}
    assert names - shown_snippets() == set(), "snippets that no page shows"
    assert shown_snippets() - names == set(), "pages that show snippets that do not exist"
    outputs = {p.name for p in rackpy_docs.SNIPPETS.glob("*.out")}
    used = {s.output.name for s in map(rackpy_docs.read_snippet, SNIPPETS) if s.output}
    assert outputs == used, "output files that no snippet uses"


def test_malformed_snippet_headers_are_rejected(tmp_path: Path) -> None:
    bad = tmp_path / "bad.py"
    bad.write_text("print('no header')\n")
    with pytest.raises(ValueError, match=re.escape("no '# rackpy-example: runs-in=...' header")):
        rackpy_docs.read_snippet(bad)
    bad.write_text("# rackpy-example: runs-in=daw\n")
    with pytest.raises(ValueError, match="runs-in must be one of"):
        rackpy_docs.read_snippet(bad)
    bad.write_text("# rackpy-example: runs-in=python output=missing.out\n")
    with pytest.raises(ValueError, match=re.escape("missing.out does not exist")):
        rackpy_docs.read_snippet(bad)
    bad.write_text("# rackpy-example: runs-in=python modules=PyOne\n")
    with pytest.raises(ValueError, match="modules= needs runs-in=rack"):
        rackpy_docs.read_snippet(bad)


def test_python_snippets_print_their_expected_output() -> None:
    for snippet in map(rackpy_docs.read_snippet, SNIPPETS):
        if snippet.runs_in == "python":
            out = run_in_python(snippet.path)
            if snippet.expected is not None:
                assert out == snippet.expected, snippet.path.name


@pytest.mark.host
@linux_x64_only()
def test_rack_snippets_print_their_expected_output(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    ran = 0
    for snippet in map(rackpy_docs.read_snippet, SNIPPETS):
        if snippet.runs_in == "rack":
            out = run_in_rack(snippet.path, make_bundle(), rack_host, tmp_path / snippet.path.stem, snippet.modules)
            if snippet.expected is not None:
                assert out == snippet.expected, f"{snippet.path.name} printed:\n{out}"
            ran += 1
    assert ran


@pytest.mark.host
@linux_x64_only()
def test_wrong_output_and_errors_are_caught_in_rack(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    wrong = tmp_path / "wrong.py"
    wrong.write_text("print('actual')\n")
    assert run_in_rack(wrong, make_bundle(), rack_host, tmp_path / "u1") != "expected\n"
    broken = tmp_path / "broken.py"
    broken.write_text("raise ValueError('deliberately broken snippet')\n")
    with pytest.raises(AssertionError, match="deliberately broken snippet"):
        run_in_rack(broken, make_bundle(), rack_host, tmp_path / "u2")
