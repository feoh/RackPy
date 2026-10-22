# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-11 on the real repository: the manifest matches the inventory, and every derived file is current."""

import subprocess

import binding_manifest as bm
import pytest

from tests.support import BIN

pytestmark = pytest.mark.unit


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(BIN / args[0]), *args[1:]], capture_output=True, text=True, check=False)


def test_manifest_is_consistent_apart_from_the_built_package() -> None:
    proc = run("check_coverage.py", "check", "--without-built-package")
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert "NOT checked against the built package" in proc.stdout


def test_bound_records_need_the_built_package() -> None:
    """Outside Rack there is no artifact, so the full check must fail, not pass quietly."""
    proc = run("check_coverage.py", "check")
    assert proc.returncode != 0 and "missing-runtime-artifact" in proc.stderr


def test_generated_files_are_current() -> None:
    proc = run("generate_bindings.py", "check")
    assert proc.returncode == 0, proc.stdout + proc.stderr


def test_the_release_bar_is_not_met_yet() -> None:
    """Partial and planned bindings must still fail the full-surface release gate."""
    proc = run("check_coverage.py", "release", "lin-x64")
    assert proc.returncode != 0
    assert "incomplete" in proc.stderr and "missing-runtime-artifact" in proc.stderr


def test_every_record_is_accounted_for() -> None:
    inputs = bm.load_inputs()
    manifest = bm.load_json(bm.MANIFEST)
    statuses = {r["status"] for r in manifest["entries"]}
    assert statuses <= {"unreviewed", "planned", "implemented", "verified", "excluded_internal"}
    assert "planned" not in bm.BOUND | bm.FINAL  # tracking a gap never completes it
    bound = [r for r in manifest["entries"] if r["status"] in bm.BOUND]
    families = {bm.family_of(inputs.inventory, inputs.index[r["id"]]) for r in bound}
    assert families <= {"1 common/value", "2 engine"}, (
        "only families 1 (RP-12) and 2 (RP-13 components / RP-14A construction) are bound so far"
    )
    counted = [e for e in inputs.index.values() if bm.counted(e)]
    assert len(manifest["entries"]) == len(counted) == 3058
    excluded = [r for r in manifest["entries"] if r["status"] == "excluded_internal"]
    assert len(excluded) == len(inputs.exclusions) == 111


def test_every_module_gap_retains_a_responsible_task() -> None:
    manifest = bm.load_json(bm.MANIFEST)
    records = [
        r
        for r in manifest["entries"]
        if r["id"].split(":", 1)[1].startswith("rack::engine::Module") and r["status"] not in bm.BOUND | bm.FINAL
    ]
    assert records
    for record in records:
        assert record["status"] in {"planned", "unreviewed"}
        assert record["notes"].startswith("Responsible task: RP-"), record["id"]
    # These safe adapters do not claim the public mutable vectors are fully mapped.
    vectors = {"params", "inputs", "outputs", "lights", "bypassRoutes"}
    incomplete = {r["id"].rsplit("::", 1)[-1] for r in records}
    assert vectors <= incomplete


@pytest.mark.parametrize(
    "symbol",
    [
        "field:rack::engine::Module::inputInfos",
        "field:rack::engine::Module::outputInfos",
        "field:rack::engine::Module::lightInfos",
        "method:rack::engine::Module::getInputInfo(int)",
        "method:rack::engine::Module::getOutputInfo(int)",
        "method:rack::engine::Module::getLightInfo(int)",
        "method_template:rack::engine::Module::configInput<type>(int, std::string)",
        "method_template:rack::engine::Module::configOutput<type>(int, std::string)",
        "method_template:rack::engine::Module::configLight<type>(int, std::string)",
    ],
)
def test_builtin_metadata_adapters_do_not_complete_raw_or_template_semantics(symbol: str) -> None:
    record = next(r for r in bm.load_json(bm.MANIFEST)["entries"] if r["id"] == symbol)
    assert record["status"] == "planned"
    assert record["python"] is None
    assert "RP-14B1" in record["notes"] and "RP-14B2" in record["notes"]
    assert record["tests"] and record["docs"]


def test_every_public_engine_method_has_a_lock_entry() -> None:
    inputs = bm.load_inputs()
    engine = [i for i, e in inputs.index.items() if bm.is_engine_method(e) and e["classification"] == "public"]
    assert engine and set(engine) == set(inputs.locks)
