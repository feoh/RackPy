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
    allowed = {
        "0 entry point",
        "1 common/value",
        "2 engine",
        "3 plugin/model/assets/helpers",
        "4 widgets/ui/app/componentlibrary",
        "5 window/rendering/context",
        "6 dsp/simd",
        "7 audio/midi",
        "8 services",
        "9 history",
    }
    assert families <= allowed, (
        "only families 0 (RP-28A's PRIVATE macro), 1 (RP-12), 2 (RP-13/RP-14/RP-15/RP-24), 3 (RP-16A plugins and "
        "models), 4 (RP-25/RP-26/RP-28 widgets), 5 (RP-26B/RP-27 window and context), 6 (RP-20 SIMD), "
        "7 (RP-29 audio/MIDI), 8 (RP-30 services) and 9 (RP-24C history) are bound so far"
    )
    counted = [e for e in inputs.index.values() if bm.counted(e)]
    # RP-31H adds 143 GLFW and 10 Blendish boundary constants to the original 3058.
    assert sum(e["kind"] == "boundary_constant" for e in counted) == 153
    assert sum(e["kind"] == "conditional_declaration" for e in counted) == 13
    assert len(manifest["entries"]) == len(counted) == 3224
    excluded = [r for r in manifest["entries"] if r["status"] == "excluded_internal"]
    assert len(excluded) == len(inputs.exclusions) == 111


def test_every_module_gap_retains_a_responsible_task() -> None:
    manifest = bm.load_json(bm.MANIFEST)
    records = [
        r
        for r in manifest["entries"]
        if r["id"].split(":", 1)[1].startswith("rack::engine::Module") and r["status"] not in bm.BOUND | bm.FINAL
    ]
    # RP-23C bound the last two (Module::Expander::producerMessage and consumerMessage); a gap that opens
    # again must name the task responsible for it.
    for record in records:
        assert record["status"] in {"planned", "unreviewed"}
        assert record["notes"].startswith("Responsible task: RP-"), record["id"]
    # Every public Module vector is mapped (RP-14B2b7, RP-14C4).
    incomplete = {r["id"].rsplit("::", 1)[-1] for r in records}
    vectors = {"params", "inputs", "outputs", "lights", "bypassRoutes", "paramQuantities", "inputInfos", "outputInfos"}
    assert not vectors & incomplete


@pytest.mark.parametrize(
    "field", ["params", "inputs", "outputs", "lights", "paramQuantities", "inputInfos", "outputInfos", "lightInfos"]
)
def test_raw_module_vectors_follow_the_constructor_scoped_contract(field: str) -> None:
    record = next(r for r in bm.load_json(bm.MANIFEST)["entries"] if r["id"] == "field:rack::engine::Module::" + field)
    assert record["status"] == "implemented" and record["python"]["kind"] == "property"
    assert record["notes"].startswith("RP-14B2b7") and "owner decision 2026-10-23" in record["notes"]
    assert "After the seal" in record["notes"] and "macOS/Windows unvalidated" in record["notes"]
    assert all("test_raw_vectors.py" in t for t in record["tests"])


@pytest.mark.parametrize(
    "symbol",
    [
        "method:rack::engine::Module::getInputInfo(int)",
        "method:rack::engine::Module::getOutputInfo(int)",
        "method:rack::engine::Module::getLightInfo(int)",
        "method_template:rack::engine::Module::configInput<type>(int, std::string)",
        "method_template:rack::engine::Module::configOutput<type>(int, std::string)",
        "method_template:rack::engine::Module::configLight<type>(int, std::string)",
    ],
)
def test_metadata_class_factories_complete_only_templates_and_getters(symbol: str) -> None:
    record = next(r for r in bm.load_json(bm.MANIFEST)["entries"] if r["id"] == symbol)
    assert record["status"] == "implemented" and record["python"]["raw"].startswith("rackpy.raw.engine.Module.")
    assert record["notes"].startswith("RP-14B2b5") and "macOS/Windows unvalidated" in record["notes"]
    # Templates transfer Python classes; raw vectors and Rack-owned models stay open.
    assert (
        "info_type" in record["notes"] and "RP-16" in record["notes"]
    ) or "Raw vector fields stay planned" in record["notes"]
    assert any("test_transferred_metadata.py" in t for t in record["tests"])


def test_quantity_records_are_mapped_with_their_laboratory_path() -> None:
    entries = bm.load_json(bm.MANIFEST)["entries"]
    records = [
        r
        for r in entries
        if r["id"].split(":", 1)[1].startswith(("rack::engine::ParamQuantity", "rack::engine::SwitchQuantity"))
    ]
    # RP-14B2b8/RP-14C2 left the mixed value/range virtuals open for RP-22's laboratory path; RP-22C mapped them.
    assert records and {r["status"] for r in records} == {"implemented"}
    mixed = [r for r in entries if r["callback"] == "mixed"]
    assert {r["id"].rsplit("::", 1)[1] for r in mixed} == {
        "getDefaultValue()",
        "getMaxValue()",
        "getMinValue()",
        "getValue()",
        "setValue(float)",
    }
    assert len(mixed) == 10  # rack::Quantity and rack::engine::ParamQuantity
    for record in mixed:
        assert record["status"] == "implemented" and "RP-22C" in record["notes"]
        assert "rackpy.laboratory" in record["notes"]
    # Quantity JSON is mapped by RP-15A.
    for record in entries:
        if "ParamQuantity::" in record["id"] and "Json(" in record["id"]:
            assert record["status"] == "implemented" and record["notes"].startswith("RP-15A")


@pytest.mark.parametrize(
    "symbol",
    [
        "method:rack::engine::Module::getParamQuantity(int)",
        "method_template:rack::engine::Module::configParam<type>"
        "(int, float, float, float, std::string, std::string, float, float, float)",
        "method_template:rack::engine::Module::configSwitch<type>"
        "(int, float, float, float, std::string, std::vector<std::string>)",
        "method_template:rack::engine::Module::configButton<type>(int, std::string)",
    ],
)
def test_quantity_class_factories_complete_only_templates_and_getter(symbol: str) -> None:
    record = next(r for r in bm.load_json(bm.MANIFEST)["entries"] if r["id"] == symbol)
    assert record["status"] == "implemented" and record["python"]["raw"].startswith("rackpy.raw.engine.Module.")
    assert record["notes"].startswith("RP-14B2b6") and "macOS/Windows unvalidated" in record["notes"]
    assert ("RP-22" in record["notes"] and "RP-16" in record["notes"]) or "stays planned" in record["notes"]
    assert any("test_transferred_quantities.py" in t for t in record["tests"])


def test_every_public_engine_method_has_a_lock_entry() -> None:
    inputs = bm.load_inputs()
    engine = [i for i, e in inputs.index.items() if bm.is_engine_method(e) and e["classification"] == "public"]
    assert engine and set(engine) == set(inputs.locks)
