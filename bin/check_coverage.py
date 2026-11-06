#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = ["cyclopts>=5.2,<6"]
#
# [tool.uv]
# exclude-newer = "2026-10-21T00:00:00Z"
# ///
"""Check the binding manifest against the inventory, the rules and the built package (RP-11).

check    fail on any inconsistency: missing or orphaned records, stale
         signatures, bogus exclusions, wrong callback or engine-lock data,
         bound records whose Python names, implementation, tests or docs do
         not exist. Partial coverage passes; false claims do not.
report   print coverage by family and platform (Markdown or JSON).
evidence every bound record available on a platform cites a test that
         passed there (test outcomes from RACKPY_TEST_EVIDENCE).
samples  every official sample's disposition tests passed on a platform.
release  the full-coverage release bar for one platform: zero records that
         are not verified or excluded_internal, none naming an open owner
         review item, a real inventory for the platform, and introspection
         of the built package.

--introspection takes the JSON that tests/coverage/test_introspection.py
writes from inside Rack (rackpy._introspect). Records that claim bound
Python names cannot pass without it.
"""

import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Literal

import cyclopts

sys.path.insert(0, str(Path(__file__).resolve().parent))
import binding_manifest as bm

app = cyclopts.App(help=__doc__)


def load(manifest: Path, introspection: Path | None) -> tuple[bm.Inputs, dict[str, Any], dict[str, Any] | None]:
    inputs = bm.load_inputs()
    data: dict[str, Any] = bm.load_json(manifest)
    artifact: dict[str, Any] | None = bm.load_json(introspection) if introspection else None
    if artifact is not None and not artifact.get("objects"):
        raise bm.ManifestError(f"{introspection} lists no objects; it is not an introspection of a built package")
    return inputs, data, artifact


def fail(problems: list[bm.Problem], limit: int) -> None:
    """Every kind of problem with its count first, so a rare one is not buried under a common one."""
    kinds = Counter(p.code for p in problems)
    summary = ", ".join(f"{code} {n}" for code, n in sorted(kinds.items()))
    # Show the rarest kinds first: they are usually the ones to fix.
    ordered = sorted(problems, key=lambda p: (kinds[p.code], p))
    shown = "\n".join(f"  {p}" for p in ordered[:limit])
    more = f"\n  ... and {len(problems) - limit} more" if len(problems) > limit else ""
    raise SystemExit(f"{len(problems)} problem(s): {summary}\n{shown}{more}")


@app.command
def check(
    *,
    manifest: Path = bm.MANIFEST,
    introspection: Path | None = None,
    without_built_package: bool = False,
    limit: int = 50,
) -> None:
    """Fail on any inconsistency between the manifest and its sources.

    Bound records need --introspection. --without-built-package checks
    everything else and says so; it is for jobs that cannot run Rack, never
    a substitute for the check with the artifact.
    """
    inputs, data, artifact = load(manifest, introspection)
    problems = bm.validate(inputs, data, artifact)
    if without_built_package:
        if introspection:
            raise SystemExit("--without-built-package and --introspection contradict each other")
        problems = [p for p in problems if p.code != "missing-runtime-artifact"]
    if problems:
        fail(problems, limit)
    rep = bm.report(inputs, data)
    print("ok: " + ", ".join(f"{k} {v}" for k, v in sorted(rep["totals"]["lin-x64"].items())))
    if rep["open_owner_reviews"]:
        print(f"note: {len(rep['open_owner_reviews'])} record(s) name an open owner review item; each blocks a release")
    if rep["known_release_gaps"]:
        print(
            f"note: {len(rep['known_release_gaps'])} owner-approved known release gap(s) remain non-verified; "
            "this is not a complete-coverage claim"
        )
    if without_built_package and any(r["status"] in bm.BOUND for r in data["entries"]):
        print("note: Python names were NOT checked against the built package (--without-built-package)")


@app.command
def report(*, manifest: Path = bm.MANIFEST, output_format: Literal["markdown", "json"] = "markdown") -> None:
    """Coverage by family and platform."""
    inputs, data, _ = load(manifest, None)
    rep = bm.report(inputs, data)
    print(json.dumps(rep, indent=2) if output_format == "json" else bm.markdown_report(rep))


@app.command
def evidence(
    platform: str, results: list[Path], *, manifest: Path = bm.MANIFEST, output: Path | None = None, limit: int = 20
) -> None:
    """Fail unless every bound record available on PLATFORM cites a test that passed there and none that failed.

    RESULTS are RACKPY_TEST_EVIDENCE directories or files from test runs on that platform. --output writes the
    per-record result as JSON (release-evidence/coverage/).
    """
    inputs, data, _ = load(manifest, None)
    outcomes = bm.load_evidence(results, platform)
    problems, counts = bm.evidence_problems(inputs, data, platform, outcomes)
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        doc = {
            "platform": platform,
            "inventory_sha256": inputs.inventory_sha256,
            "tests_run": len(outcomes),
            "test_outcomes": dict(sorted(Counter(outcomes.values()).items())),
            "records": dict(sorted(counts.items())),
            "problems": [{"code": p.code, "id": p.id, "message": p.message} for p in problems],
        }
        output.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    summary = ", ".join(f"{k} {v}" for k, v in sorted(counts.items()))
    if problems:
        print(f"{platform}: {summary}")
        fail(problems, limit)
    print(f"ok: {platform}: {summary}; {len(outcomes)} tests ran")


@app.command
def samples(
    platform: str, results: list[Path], *, examples: Path = bm.EXAMPLES, output: Path | None = None, limit: int = 20
) -> None:
    """Check official sample disposition tests on PLATFORM (sample parity, RP-42).

    RESULTS are RACKPY_TEST_EVIDENCE directories or files from test runs on that platform. A skipped or unrun test
    is not a pass except for explicitly listed platform guard skips in upstream/examples.json. --output writes the
    per-sample result as JSON (release-evidence/samples/).
    """
    data: dict[str, Any] = bm.load_json(examples)
    outcomes = bm.load_evidence(results, platform)
    problems, per_sample = bm.sample_problems(data["samples"], platform, outcomes)
    counts = Counter(r["status"] for r in per_sample.values())
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        doc = {
            "platform": platform,
            "examples_retrieved_at": data["retrieved_at"],
            "tests_run": len(outcomes),
            "counts": dict(sorted(counts.items())),
            "samples": per_sample,
            "problems": [{"code": p.code, "id": p.id, "message": p.message} for p in problems],
        }
        output.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    summary = ", ".join(f"{k} {v}" for k, v in sorted(counts.items()))
    if problems:
        print(f"{platform}: {summary}")
        fail(problems, limit)
    print(f"ok: {platform}: {summary}")


@app.command
def citations(*, manifest: Path = bm.MANIFEST, output: Path | None = None) -> None:
    """A review aid, never a gate: bound records whose cited tests never name their Python binding.

    A data-driven test or a syntax call can exercise a binding without naming it, so each listed record needs a
    reviewer's look, not an automatic fix. --output writes the list as JSON.
    """
    data: dict[str, Any] = bm.load_json(manifest)
    weak = bm.weak_citations(data)
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        rows = [{"id": p.id, "message": p.message} for p in weak]
        output.write_text(json.dumps(rows, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    for p in weak:
        print(f"  {p}")
    print(f"{len(weak)} record(s) for a reviewer to check")


@app.command
def release(
    platform: str,
    *,
    manifest: Path = bm.MANIFEST,
    introspection: Path | None = None,
    evidence: list[Path] | None = None,
    limit: int = 20,
) -> None:
    """Fail unless PLATFORM meets the audited release gate (--evidence: its test outcomes).

    Owner-approved planned/unrepresentable gaps are permitted only when explicitly marked in the manifest and listed
    in docs/coverage.md; a passing partial-release gate is not a complete-coverage claim.
    """
    inputs, data, artifact = load(manifest, introspection)
    outcomes = bm.load_evidence(evidence, platform) if evidence else None
    problems = bm.release_problems(inputs, data, platform, artifact, outcomes)
    if problems:
        fail(problems, limit)
    print(f"ok: {platform} meets the audited release gate (known gaps remain non-verified)")


if __name__ == "__main__":
    try:
        app()
    except bm.ManifestError as e:
        raise SystemExit(f"error: {e}") from None
