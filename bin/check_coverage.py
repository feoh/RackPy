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
release  the full-coverage release bar for one platform: zero records that
         are not verified or excluded_internal, a real inventory for the
         platform, and introspection of the built package.

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
def check(*, manifest: Path = bm.MANIFEST, introspection: Path | None = None, limit: int = 50) -> None:
    """Fail on any inconsistency between the manifest and its sources."""
    inputs, data, artifact = load(manifest, introspection)
    problems = bm.validate(inputs, data, artifact)
    if problems:
        fail(problems, limit)
    totals = bm.report(inputs, data)["totals"]["lin-x64"]
    print("ok: " + ", ".join(f"{k} {v}" for k, v in sorted(totals.items())))


@app.command
def report(*, manifest: Path = bm.MANIFEST, output_format: Literal["markdown", "json"] = "markdown") -> None:
    """Coverage by family and platform."""
    inputs, data, _ = load(manifest, None)
    rep = bm.report(inputs, data)
    print(json.dumps(rep, indent=2) if output_format == "json" else bm.markdown_report(rep))


@app.command
def release(platform: str, *, manifest: Path = bm.MANIFEST, introspection: Path | None = None, limit: int = 20) -> None:
    """Fail unless PLATFORM meets the full-coverage release bar."""
    inputs, data, artifact = load(manifest, introspection)
    problems = bm.release_problems(inputs, data, platform, artifact)
    if problems:
        fail(problems, limit)
    print(f"ok: {platform} meets the full-coverage release bar")


if __name__ == "__main__":
    try:
        app()
    except bm.ManifestError as e:
        raise SystemExit(f"error: {e}") from None
