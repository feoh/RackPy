# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-11: the manifest's rules, on a miniature inventory where every failure can be provoked."""

from typing import Any

import binding_manifest as bm
import generate_bindings as gb
import pytest

from tests.coverage.conftest import Mini, bind, codes

pytestmark = pytest.mark.unit
PROCESS = "method:rack::engine::Module::process(const ProcessArgs &)"
VOLTAGE = "method:rack::engine::Port::getVoltage(uint8_t)"
VOLTAGE0 = "method:rack::engine::Port::getVoltage()"
ADD = "method:rack::engine::Engine::addModule(rack::engine::Module *)"
INTERNAL = "method:rack::engine::Engine::setSampleRate(float)"
ALIAS = "alias:rack::app::SVGPanel"


def by_id(manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {r["id"]: r for r in manifest["entries"]}


def test_seed_records_every_counted_declaration_and_promotes_nothing(mini: Mini) -> None:
    manifest = mini.seeded()
    records = by_id(manifest)
    assert "import:rack:using plugin::Model" not in records
    assert len(records) == len(mini.inventory["entries"]) - 1
    assert {r["status"] for i, r in records.items() if i != INTERNAL} == {"unreviewed"}
    assert records[INTERNAL]["status"] == "excluded_internal" and records[INTERNAL]["reviewer"] == "test"
    assert records[PROCESS]["callback"] == "engine"
    assert records[ADD]["locks_engine"] == "exclusive"
    assert bm.validate(mini.inputs(), manifest) == []


def test_seed_keeps_reviews_resets_changed_declarations_and_reports_removed(mini: Mini) -> None:
    manifest = mini.seeded()
    records = by_id(manifest)
    records[VOLTAGE].update(status="planned", notes="next")
    records[VOLTAGE0].update(status="planned")
    mini.inventory["entries"] = [e for e in mini.inventory["entries"] if e["id"] != ALIAS]
    changed = next(e for e in mini.inventory["entries"] if e["id"] == VOLTAGE)
    changed["returns"] = "double"
    again, changes = bm.seed(mini.inputs(), manifest)
    after = by_id(again)
    assert after[VOLTAGE]["status"] == "unreviewed" and "was planned" in after[VOLTAGE]["notes"]
    assert after[VOLTAGE0]["status"] == "planned"  # unchanged declaration keeps its review
    assert changes.reset == [VOLTAGE] and changes.removed == [ALIAS] and changes.added == []


def test_conditional_branch_declarations_need_separate_records(mini: Mini) -> None:
    mini.inventory["conditional_blocks"] = [
        {
            "header": "include/arch.hpp",
            "lines": [12, 14],
            "condition": "#if defined _WIN32",
            "sha256": "a",
            "skipped_in": ["lin-x64"],
            "declarations": ["macro ARCH_WIN"],
        },
        {
            "header": "include/common.hpp",
            "lines": [31, 33],
            "condition": "#ifndef PRIVATE",
            "sha256": "b",
            "skipped_in": ["lin-x64-simde"],
            "declarations": ["macro PRIVATE"],
        },
    ]
    manifest = mini.seeded()
    win = "conditional:include/arch.hpp:12:macro ARCH_WIN"
    unknown = "conditional:include/common.hpp:31:macro PRIVATE"
    assert by_id(manifest)[win]["status"] == "unreviewed"
    assert bm.availability(mini.inventory, mini.inputs().index[win]) == {
        "lin-x64": "no",
        "mac-x64": "not inventoried",
        "mac-arm64": "not inventoried",
        "win-x64": "not inventoried",
    }
    assert bm.availability(mini.inventory, mini.inputs().index[unknown])["lin-x64"] == "not inventoried"
    assert "not-inventoried" in codes(bm.release_problems(mini.inputs(), manifest, "lin-x64", {"objects": {}}))
    by_id(manifest)[unknown]["status"] = "verified"
    assert "conditional-unresolved" in codes(bm.validate(mini.inputs(), manifest))
    by_id(manifest)[unknown]["status"] = "unreviewed"
    mini.inventory["conditional_blocks"][0]["sha256"] = "changed"
    assert win in {p.id for p in bm.validate(mini.inputs(), manifest) if p.code == "stale-signature"}
    mini.inventory["conditional_blocks"][0]["sha256"] = "a"
    manifest["entries"] = [r for r in manifest["entries"] if r["id"] != win]
    assert win in {p.id for p in bm.validate(mini.inputs(), manifest) if p.code == "missing"}
    mini.inventory["conditional_blocks"][0]["declarations"].append("function future")
    assert "conditional:include/arch.hpp:12:function future" in {
        p.id for p in bm.validate(mini.inputs(), manifest) if p.code == "missing"
    }


def test_inventory_without_conditional_blocks_is_refused(mini: Mini) -> None:
    del mini.inventory["conditional_blocks"]
    with pytest.raises(bm.ManifestError, match="conditional_blocks"):
        mini.seeded()


def test_a_missing_overload_is_unknown(mini: Mini) -> None:
    manifest = mini.seeded()
    manifest["entries"] = [r for r in manifest["entries"] if r["id"] != VOLTAGE0]
    problems = bm.validate(mini.inputs(), manifest)
    assert [p.id for p in problems if p.code == "missing"] == [VOLTAGE0]


def test_orphaned_and_duplicate_records_fail(mini: Mini) -> None:
    manifest = mini.seeded()
    ghost = {**manifest["entries"][0], "id": "method:rack::Ghost::boo()"}
    manifest["entries"] += [ghost, dict(manifest["entries"][1])]
    assert {"orphaned", "duplicate"} <= codes(bm.validate(mini.inputs(), manifest))


def test_a_changed_signature_is_stale(mini: Mini) -> None:
    manifest = mini.seeded()
    next(e for e in mini.inventory["entries"] if e["id"] == VOLTAGE)["params"] = [{"type": "int"}]
    problems = bm.validate(mini.inputs(), manifest)
    assert {"stale-signature", "stale-inventory"} <= codes(problems)
    assert [p.id for p in problems if p.code == "stale-signature"] == [VOLTAGE]


def test_moving_a_declaration_is_not_a_change(mini: Mini) -> None:
    manifest = mini.seeded()
    next(e for e in mini.inventory["entries"] if e["id"] == VOLTAGE)["line"] = 99
    assert "stale-signature" not in codes(bm.validate(mini.inputs(), manifest))


def test_bogus_exclusions_fail(mini: Mini) -> None:
    manifest = mini.seeded()
    by_id(manifest)[VOLTAGE].update(status="excluded_internal", notes="too hard", reviewer="me")
    mini.exclusions[VOLTAGE0] = {"id": VOLTAGE0, "evidence": "", "reason": "too hard", "reviewer": "me"}
    problems = bm.validate(mini.inputs(), manifest)
    bogus = {p.id for p in problems if p.code == "bogus-exclusion"}
    assert {VOLTAGE, VOLTAGE0} <= bogus


def test_an_internal_declaration_cannot_be_bound_or_left_unexcluded(mini: Mini) -> None:
    manifest = mini.seeded()
    by_id(manifest)[INTERNAL]["status"] = "planned"
    assert "internal-not-excluded" in codes(bm.validate(mini.inputs(), manifest))


def test_bound_records_need_real_evidence(mini: Mini) -> None:
    manifest = mini.seeded()
    record = by_id(manifest)[VOLTAGE]
    record.update(
        status="implemented",
        python={"raw": "rackpy.raw.engine.Port.get_voltage", "kind": "method"},
        implementation="native/bindings/nowhere.cpp",
        tests=["tests/runtime/test_missing.py::test_nothing"],
        docs=["docs/reference/missing.md#nothing"],
    )
    problems = bm.validate(mini.inputs(), manifest, {"objects": {"rackpy.raw.engine.Port.get_voltage": {}}})
    assert {"implementation", "tests", "docs"} <= codes(problems)


def test_bound_records_need_the_built_package(mini: Mini) -> None:
    manifest = mini.seeded()
    bind(by_id(manifest)[VOLTAGE], mini, raw="rackpy.raw.engine.Port.get_voltage")
    assert "missing-runtime-artifact" in codes(bm.validate(mini.inputs(), manifest, None))
    absent = bm.validate(mini.inputs(), manifest, {"objects": {"rackpy.raw": {}}})
    assert "not-exported" in codes(absent)
    present = bm.validate(
        mini.inputs(), manifest, {"objects": {"rackpy.raw.engine.Port.get_voltage": {"kind": "function"}}}
    )
    assert present == []


def test_a_not_implemented_stub_is_not_an_implementation(mini: Mini) -> None:
    mini.write("src/rackpy/facade.py", "class Port:\n    def get_voltage(self):\n        raise NotImplementedError\n")
    manifest = mini.seeded()
    bind(
        by_id(manifest)[VOLTAGE],
        mini,
        raw="rackpy.raw.engine.Port.get_voltage",
        facade="rackpy.facade.Port.get_voltage",
    )
    objects: dict[str, dict[str, Any]] = {
        "rackpy.raw.engine.Port.get_voltage": {},
        "rackpy.facade.Port.get_voltage": {},
    }
    assert "not-implemented" in codes(bm.validate(mini.inputs(), manifest, {"objects": objects}))
    objects["rackpy.facade.Port.get_voltage"] = {"not_implemented": True}
    mini.write("src/rackpy/facade.py", "class Port:\n    def get_voltage(self):\n        return 0.0\n")
    assert "not-implemented" in codes(bm.validate(mini.inputs(), manifest, {"objects": objects}))


def test_an_alias_is_complete_only_with_its_target(mini: Mini) -> None:
    manifest = mini.seeded()
    bind(by_id(manifest)[ALIAS], mini, raw="rackpy.raw.app.SVGPanel", kind="alias")
    artifact: dict[str, Any] = {"objects": {"rackpy.raw.app.SVGPanel": {}}}
    assert "alias" in codes(bm.validate(mini.inputs(), manifest, artifact))


def test_engine_locking_bindings_must_release_the_gil(mini: Mini) -> None:
    manifest = mini.seeded()
    record = by_id(manifest)[ADD]
    bind(record, mini, raw="rackpy.raw.engine.Engine.add_module")
    artifact: dict[str, Any] = {"objects": {"rackpy.raw.engine.Engine.add_module": {}}}
    assert "gil" in codes(bm.validate(mini.inputs(), manifest, artifact))
    with pytest.raises(bm.ManifestError, match="do not release the GIL"):
        gb.generate_files(manifest, bm.dumps(manifest), mini.inputs(), mini.repo)
    record["python"]["gil_release"] = True
    assert bm.validate(mini.inputs(), manifest, artifact) == []


def test_an_unresolved_callback_cannot_be_planned(mini: Mini) -> None:
    mini.strategies[PROCESS] = "unresolved"
    manifest = mini.seeded()
    by_id(manifest)[PROCESS]["status"] = "planned"
    assert "unresolved-callback" in codes(bm.validate(mini.inputs(), manifest))


def test_derived_fields_must_match_their_sources(mini: Mini) -> None:
    manifest = mini.seeded()
    by_id(manifest)[PROCESS]["callback"] = "ui"
    by_id(manifest)[ADD]["locks_engine"] = "none"
    del mini.locks["method:rack::engine::Engine::getNumModules()"]
    assert {"callback", "engine-lock", "engine-lock-unknown"} <= codes(bm.validate(mini.inputs(), manifest))


def test_empty_or_unreadable_inputs_fail_instead_of_reporting_nothing(mini: Mini) -> None:
    mini.inventory["entries"] = []
    with pytest.raises(bm.ManifestError, match="no entries"):
        bm.seed(mini.inputs(), None)
    with pytest.raises(bm.ManifestError, match="does not exist"):
        bm.load_json(mini.repo / "missing.json")
    broken = mini.write("broken.json", "{not json")
    with pytest.raises(bm.ManifestError, match="not valid JSON"):
        bm.load_json(broken)
    assert "empty" in codes(
        bm.validate(
            Mini(
                mini.repo,
                mini.inventory
                | {
                    "entries": [
                        {"classification": "public", "id": "x", "header": "h", "kind": "k", "name": "n", "line": 1}
                    ]
                },
                {},
            ).inputs(),
            {"schema_version": 1, "entries": []},
        )
    )


def test_generation_is_deterministic_and_edits_are_detected(mini: Mini) -> None:
    manifest = mini.seeded()
    text = bm.dumps(manifest)
    first = gb.generate_files(manifest, text, mini.inputs(), mini.repo)
    second = gb.generate_files(bm.load_json(mini.write("bindings/manifest.json", text)), text, mini.inputs(), mini.repo)
    assert first == second
    bind(
        by_id(manifest)[VOLTAGE],
        mini,
        raw="rackpy.raw.engine.Port.get_voltage",
    )
    changed = gb.generate_files(manifest, bm.dumps(manifest), mini.inputs(), mini.repo)
    assert changed != first
    registry = next(c for p, c in changed.items() if p.name == "registry.cpp")
    assert "bind_engine(raw);" in registry and VOLTAGE in registry


def test_generation_needs_an_adapter_for_each_bound_family(mini: Mini) -> None:
    manifest = mini.seeded()
    bind(by_id(manifest)[VOLTAGE], mini, raw="rackpy.raw.engine.Port.get_voltage")
    (mini.repo / "native" / "bindings" / "engine.cpp").unlink()
    with pytest.raises(bm.ManifestError, match="no hand-written adapter"):
        gb.generate_files(manifest, bm.dumps(manifest), mini.inputs(), mini.repo)


def test_release_needs_everything_final_and_real_artifacts(mini: Mini) -> None:
    manifest = mini.seeded()
    assert "incomplete" in codes(bm.release_problems(mini.inputs(), manifest, "lin-x64", {"objects": {"x": {}}}))
    assert "not-inventoried" in codes(bm.release_problems(mini.inputs(), manifest, "win-x64", {"objects": {"x": {}}}))
    objects: dict[str, Any] = {}
    for record in manifest["entries"]:
        if record["status"] == "excluded_internal":
            continue
        raw = "rackpy.raw.x" + str(len(objects))
        bind(record, mini, raw=raw, gil_release=True)
        record.update(status="verified", reviewer="test")
        objects[raw] = {}
    assert bm.release_problems(mini.inputs(), manifest, "lin-x64", {"objects": objects}) == []
    assert "missing-runtime-artifact" in codes(bm.release_problems(mini.inputs(), manifest, "lin-x64", None))


def test_report_counts_every_declaration_once(mini: Mini) -> None:
    rep = bm.report(mini.inputs(), mini.seeded())
    assert sum(rep["totals"]["lin-x64"].values()) == len(mini.inventory["entries"]) - 1
    assert rep["totals"]["win-x64"] == {"not inventoried": len(mini.inventory["entries"]) - 1}


def test_only_macros_and_declared_but_undefined_functions_map_to_nothing(mini: Mini) -> None:
    mini.write("docs/reference/engine.md", "# Engine\n\n## Port voltages\n")
    manifest = mini.seeded()
    record = by_id(manifest)[VOLTAGE]
    record.update(
        status="implemented",
        python={"raw": None, "facade": None, "kind": "no_runtime_meaning", "gil_release": False},
        docs=["docs/reference/engine.md#port-voltages"],
        notes="hard to bind",
    )
    assert "no-runtime-meaning" in codes(bm.validate(mini.inputs(), manifest, {"objects": {}}))
    record["notes"] = bm.DECLARED_UNDEFINED + ": declared in Port.hpp, libRack exports no symbol for it"
    assert "no-runtime-meaning" not in codes(bm.validate(mini.inputs(), manifest, {"objects": {}}))
    record["python"]["raw"] = "rackpy.raw.engine.Port.get_voltage"
    assert "no-runtime-meaning" in codes(bm.validate(mini.inputs(), manifest, {"objects": {}}))


def test_an_open_owner_review_item_blocks_a_release_and_is_reported(mini: Mini) -> None:
    manifest = mini.seeded()
    objects: dict[str, Any] = {}
    for record in manifest["entries"]:
        if record["status"] == "excluded_internal":
            continue
        raw = "rackpy.raw.x" + str(len(objects))
        bind(record, mini, raw=raw, gil_release=True)
        record.update(status="verified", reviewer="test")
        objects[raw] = {}
    record = by_id(manifest)[VOLTAGE]
    record["notes"] = "Mapped for channels 0 to 15. Open owner review item: the channel bound of 16. Linux only."
    assert bm.validate(mini.inputs(), manifest, {"objects": objects}) == []  # check passes: partial coverage
    problems = bm.release_problems(mini.inputs(), manifest, "lin-x64", {"objects": objects})
    assert [(p.code, p.id) for p in problems] == [("owner-review", VOLTAGE)]  # even though it is verified
    assert "the channel bound of 16" in problems[0].message
    rep = bm.report(mini.inputs(), manifest)
    assert rep["open_owner_reviews"] == {VOLTAGE: ["the channel bound of 16"]}
    text = bm.markdown_report(rep)
    assert "**Open owner review items** (each blocks a release until the owner decides): 1" in text
    assert f"- `{VOLTAGE}`: the channel bound of 16" in text
    record["notes"] = "Owner-approved 2026-10-27: the channel bound of 16."
    assert bm.release_problems(mini.inputs(), manifest, "lin-x64", {"objects": objects}) == []
    assert bm.report(mini.inputs(), manifest)["open_owner_reviews"] == {}


@pytest.mark.parametrize(
    "notes",
    [
        "RackPy's bounded contract, an owner review item.",  # the pre-RP-31K wording: unreadable by the gate
        "open owner review item: lower case",
        "Owner Review Item: the bound",
        "Open owner review item:",  # names no item
        "Open owner review item: .",
    ],
)
def test_an_owner_review_item_the_gate_cannot_read_fails_the_check(mini: Mini, notes: str) -> None:
    manifest = mini.seeded()
    by_id(manifest)[VOLTAGE]["notes"] = notes
    assert "owner-review-marker" in codes(bm.validate(mini.inputs(), manifest, {"objects": {}}))
    assert bm.open_owner_reviews(by_id(manifest)[VOLTAGE]) == []
