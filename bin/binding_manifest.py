# SPDX-License-Identifier: GPL-3.0-or-later
"""The binding manifest: schema, seeding, validation and coverage reports (RP-11).

Imported by bin/generate_bindings.py and bin/check_coverage.py; not a command.

bindings/manifest.json holds one reviewed record per counted declaration in
upstream/api-surface.json (every entry except imports). The inventory says what
exists; the manifest says what RackPy does about it. Nothing here ever marks a
record implemented or verified: people do that, with evidence the checks can
follow (a Python name the built package exports, an implementation file, test
node IDs, documentation anchors).

The rules are docs/coverage.md's completeness policy; docs/contributing-bindings.md
explains how a family task updates records.
"""

from __future__ import annotations

import ast
import hashlib
import importlib.machinery
import importlib.util
import json
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from types import ModuleType
from typing import Any

REPO = Path(__file__).resolve().parent.parent
INVENTORY = REPO / "upstream" / "api-surface.json"
MANIFEST = REPO / "bindings" / "manifest.json"
EXCLUSIONS = REPO / "bindings" / "exclusions.json"
ENGINE_LOCKS = REPO / "upstream" / "engine-locks.json"
CALLBACK_RULES = REPO / "upstream" / "callback-rules.json"
CALLBACK_MATRIX = REPO / "bin" / "callback-matrix"

SCHEMA_VERSION = 1
STATUSES = ("unreviewed", "planned", "implemented", "verified", "unrepresentable", "excluded_internal")
# A full-coverage release needs every available record in one of these (docs/coverage.md, release bar).
FINAL = frozenset({"verified", "excluded_internal"})
BOUND = frozenset({"implemented", "verified"})
PLATFORMS = ("lin-x64", "mac-x64", "mac-arm64", "win-x64")
PYTHON_KINDS = frozenset(
    {
        "module",
        "class",
        "function",
        "method",
        "staticmethod",
        "classmethod",
        "property",
        "attribute",
        "constant",
        "enum",
        "enum_member",
        "factory",
        "context_manager",
        "alias",
    }
)
LOCKS = frozenset({"exclusive", "shared", "none", "caller_holds"})
ENTRY_FIELDS = (
    "id",
    "fingerprint",
    "status",
    "python",
    "callback",
    "locks_engine",
    "ownership",
    "thread",
    "implementation",
    "tests",
    "docs",
    "notes",
    "reviewer",
)
PYTHON_FIELDS = frozenset({"raw", "facade", "kind", "gil_release", "stub"})
DOTTED = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$")
FAMILY_SLUGS = {
    "0 entry point": "entry_point",
    "1 common/value": "common",
    "2 engine": "engine",
    "3 plugin/model/assets/helpers": "plugin",
    "4 widgets/ui/app/componentlibrary": "widgets",
    "5 window/rendering/context": "window",
    "6 dsp/simd": "dsp",
    "7 audio/midi": "audio_midi",
    "8 services": "services",
    "9 history": "history",
}


class ManifestError(Exception):
    """An input is missing, unreadable or empty: a setup error, never an empty report."""


@dataclass(frozen=True, order=True)
class Problem:
    code: str
    id: str
    message: str

    def __str__(self) -> str:
        return f"{self.code}: {self.id}: {self.message}"


# --- inputs ----------------------------------------------------------------------


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as e:
        raise ManifestError(f"{path} does not exist") from e
    except json.JSONDecodeError as e:
        raise ManifestError(f"{path} is not valid JSON: {e}") from e


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def counted(entry: dict[str, Any]) -> bool:
    """Imports determine Python name paths but are not declarations (docs/coverage.md, policy 7)."""
    return bool(entry["classification"] != "import")


def inventory_index(inventory: dict[str, Any]) -> dict[str, dict[str, Any]]:
    entries = inventory.get("entries")
    if not isinstance(entries, list) or not entries:
        raise ManifestError("the inventory has no entries; refusing to report coverage of nothing")
    index: dict[str, dict[str, Any]] = {}
    for e in entries:
        if e["id"] in index:
            raise ManifestError(f"the inventory lists {e['id']} twice")
        index[e["id"]] = e
    if not any(counted(e) for e in entries):
        raise ManifestError("the inventory has no counted declarations")
    return index


def fingerprint(entry: dict[str, Any]) -> str:
    """Everything that defines a declaration except its line number, so moving it is not a change."""
    canonical = json.dumps({k: v for k, v in entry.items() if k != "line"}, sort_keys=True, ensure_ascii=False)
    return hashlib.sha256(canonical.encode()).hexdigest()[:16]


def family_of(inventory: dict[str, Any], entry: dict[str, Any]) -> str:
    families: dict[str, str] = inventory["families"]
    return families.get(entry["header"], "unassigned")


def callback_matrix() -> ModuleType:
    loader = importlib.machinery.SourceFileLoader("callback_matrix", str(CALLBACK_MATRIX))
    spec = importlib.util.spec_from_loader("callback_matrix", loader)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules.setdefault("callback_matrix", module)
    loader.exec_module(module)
    return module


def callback_strategies(inventory: dict[str, Any], rules: dict[str, Any]) -> dict[str, str | None]:
    """The RP-06 strategy of every public virtual method, from upstream/callback-rules.json."""
    rows = callback_matrix().classify(inventory["entries"], rules["rules"])
    return {r["id"]: r["strategy"] for r in rows}


def engine_locks(doc: dict[str, Any]) -> dict[str, str]:
    locks: dict[str, str] = {}
    for m in doc["methods"]:
        if m["lock"] not in LOCKS:
            raise ManifestError(f"upstream/engine-locks.json: {m['id']} has unknown lock {m['lock']!r}")
        locks[m["id"]] = m["lock"]
    return locks


def is_engine_method(entry: dict[str, Any]) -> bool:
    return entry["kind"] == "method" and entry["name"].startswith("rack::engine::Engine::") and counted(entry)


def availability(inventory: dict[str, Any], entry: dict[str, Any]) -> dict[str, str]:
    """Per platform: "yes", "no" (absent there) or "not inventoried" (no extraction yet, so unknown)."""
    ok = {r["variant"] for r in inventory["extractions"] if r["status"] == "ok"}
    absent = set(entry.get("absent_in", ()))
    out = {}
    for platform in PLATFORMS:
        if platform not in ok:
            out[platform] = "not inventoried"
        else:
            out[platform] = "no" if platform in absent else "yes"
    return out


@dataclass
class Inputs:
    inventory: dict[str, Any]
    inventory_sha256: str
    exclusions: dict[str, dict[str, Any]]
    strategies: dict[str, str | None]
    locks: dict[str, str]
    repo: Path = REPO

    @property
    def index(self) -> dict[str, dict[str, Any]]:
        return inventory_index(self.inventory)


def load_inputs(
    inventory: Path = INVENTORY,
    exclusions: Path = EXCLUSIONS,
    rules: Path = CALLBACK_RULES,
    locks: Path = ENGINE_LOCKS,
    repo: Path = REPO,
) -> Inputs:
    inv = load_json(inventory)
    inventory_index(inv)  # fail early on an empty or broken inventory
    excl = load_json(exclusions)
    return Inputs(
        inventory=inv,
        inventory_sha256=sha256(inventory),
        exclusions={e["id"]: e for e in excl["entries"]},
        strategies=callback_strategies(inv, load_json(rules)),
        locks=engine_locks(load_json(locks)),
        repo=repo,
    )


# --- seeding ---------------------------------------------------------------------


def new_record(entry: dict[str, Any], inputs: Inputs) -> dict[str, Any]:
    excluded = inputs.exclusions.get(entry["id"])
    return {
        "id": entry["id"],
        "fingerprint": fingerprint(entry),
        "status": "excluded_internal" if excluded else "unreviewed",
        "python": None,
        "callback": inputs.strategies.get(entry["id"]),
        "locks_engine": inputs.locks.get(entry["id"]) if is_engine_method(entry) else None,
        "ownership": None,
        "thread": None,
        "implementation": None,
        "tests": [],
        "docs": [],
        "notes": excluded["reason"] if excluded else None,
        "reviewer": excluded["reviewer"] if excluded else None,
    }


@dataclass
class SeedChanges:
    added: list[str] = field(default_factory=list)
    removed: list[str] = field(default_factory=list)
    reset: list[str] = field(default_factory=list)  # reviewed records whose declaration changed


def seed(inputs: Inputs, existing: dict[str, Any] | None) -> tuple[dict[str, Any], SeedChanges]:
    """A manifest with one record per counted declaration.

    Records that already exist keep their review. A record whose declaration
    changed upstream goes back to unreviewed, with a note saying what it was;
    a record whose declaration disappeared is dropped and reported. Nothing is
    ever promoted.
    """
    old = {r["id"]: r for r in existing["entries"]} if existing else {}
    changes = SeedChanges()
    records = []
    for entry in sorted(inputs.index.values(), key=lambda e: e["id"]):
        if not counted(entry):
            continue
        record = old.get(entry["id"])
        fresh = new_record(entry, inputs)
        if record is None:
            changes.added.append(entry["id"])
            records.append(fresh)
            continue
        record = {k: record.get(k, fresh[k]) for k in ENTRY_FIELDS}
        # Derived fields always follow their sources.
        record["callback"] = fresh["callback"]
        record["locks_engine"] = fresh["locks_engine"]
        if record["fingerprint"] != fresh["fingerprint"]:
            if record["status"] != "unreviewed":
                changes.reset.append(entry["id"])
                record["notes"] = f"declaration changed upstream; was {record['status']}. " + (record["notes"] or "")
            record["status"] = "unreviewed" if entry["id"] not in inputs.exclusions else "excluded_internal"
            record["fingerprint"] = fresh["fingerprint"]
        records.append(record)
    changes.removed = sorted(set(old) - {r["id"] for r in records})
    head = {
        "schema_version": SCHEMA_VERSION,
        "note": "Reviewed C++ -> Python mapping for every counted declaration in upstream/api-surface.json. "
        "Edit records by hand (docs/contributing-bindings.md); bin/generate_bindings.py seed keeps them "
        "in step with the inventory and never promotes a status.",
        "inventory": {
            "path": "upstream/api-surface.json",
            "sha256": inputs.inventory_sha256,
            "rack_commit": inputs.inventory["sdk"]["rack_commit"],
        },
    }
    return {**head, "entries": records}, changes


def dumps(manifest: dict[str, Any]) -> str:
    """Pretty header, one compact record per line, like the inventory."""
    head = {k: v for k, v in manifest.items() if k != "entries"}
    text = json.dumps(head, indent=2, sort_keys=True, ensure_ascii=False)
    lines = ",\n".join(
        "    " + json.dumps({k: r[k] for k in ENTRY_FIELDS}, ensure_ascii=False) for r in manifest["entries"]
    )
    return text[:-2] + ',\n  "entries": [\n' + lines + "\n  ]\n}\n"


# --- validation ------------------------------------------------------------------


def test_exists(repo: Path, node: str) -> bool:
    """`tests/x/test_y.py::test_name` (optionally `::Class::test_name` or with `[param]`) names a real test."""
    path, _, rest = node.partition("::")
    file = repo / path
    if not rest or not file.is_file() or not path.startswith("tests/"):
        return False
    names = [part.split("[", 1)[0] for part in rest.split("::")]
    try:
        tree = ast.parse(file.read_text(encoding="utf-8"))
    except SyntaxError:
        return False
    scope: list[ast.stmt] = tree.body
    for name in names:
        found = next(
            (
                n
                for n in scope
                if isinstance(n, ast.FunctionDef | ast.AsyncFunctionDef | ast.ClassDef) and n.name == name
            ),
            None,
        )
        if found is None:
            return False
        scope = found.body if isinstance(found, ast.ClassDef) else []
    return True


def slug(heading: str) -> str:
    """MyST/GitHub heading anchor."""
    text = re.sub(r"[^\w\- ]", "", heading.strip().lower())
    return text.replace(" ", "-")


def doc_anchor_exists(repo: Path, ref: str) -> bool:
    """`docs/page.md#anchor` names a page in docs/ and a heading or `(label)=` target on it."""
    path, _, anchor = ref.partition("#")
    file = repo / path
    if not path.startswith("docs/") or not file.is_file():
        return False
    if not anchor:
        return True
    text = file.read_text(encoding="utf-8")
    headings = {slug(m) for m in re.findall(r"^#{1,6}\s+(.+?)\s*$", text, re.MULTILINE)}
    labels = set(re.findall(r"^\(([\w\-.:]+)\)=\s*$", text, re.MULTILINE))
    return anchor in headings or anchor in labels


def not_implemented_names(repo: Path) -> set[str]:
    """Qualified names of Python functions under src/rackpy whose body raises NotImplementedError."""
    found: set[str] = set()
    src = repo / "src"
    for file in sorted(src.rglob("*.py")):
        module = ".".join(file.relative_to(src).with_suffix("").parts).removesuffix(".__init__")
        tree = ast.parse(file.read_text(encoding="utf-8"))

        def visit(nodes: list[ast.stmt], prefix: str) -> None:
            for node in nodes:
                if isinstance(node, ast.ClassDef):
                    visit(node.body, f"{prefix}.{node.name}")
                elif isinstance(node, ast.FunctionDef | ast.AsyncFunctionDef):
                    for sub in ast.walk(node):
                        if isinstance(sub, ast.Raise) and sub.exc is not None:
                            target = sub.exc.func if isinstance(sub.exc, ast.Call) else sub.exc
                            if isinstance(target, ast.Name) and target.id == "NotImplementedError":
                                found.add(f"{prefix}.{node.name}")

        visit(tree.body, module)
    return found


def validate(inputs: Inputs, manifest: dict[str, Any], introspection: dict[str, Any] | None = None) -> list[Problem]:
    """Every way the manifest can disagree with the inventory, the rules or the evidence it cites."""
    problems: list[Problem] = []
    index = inputs.index
    repo = inputs.repo

    def add(code: str, ident: str, message: str) -> None:
        problems.append(Problem(code, ident, message))

    if manifest.get("schema_version") != SCHEMA_VERSION:
        add("schema", "-", f"schema_version must be {SCHEMA_VERSION}")
    recorded = manifest.get("inventory", {})
    if recorded.get("sha256") != inputs.inventory_sha256:
        add(
            "stale-inventory",
            "-",
            "upstream/api-surface.json changed since the manifest was seeded; "
            "run bin/generate_bindings.py seed --update and review the reset records",
        )
    entries = manifest.get("entries")
    if not isinstance(entries, list) or not entries:
        add("empty", "-", "the manifest has no records")
        return sorted(problems)

    seen: set[str] = set()
    records: dict[str, dict[str, Any]] = {}
    for r in entries:
        ident = r.get("id", "?")
        if ident in seen:
            add("duplicate", ident, "listed twice")
        seen.add(ident)
        records[ident] = r
        if tuple(r) != ENTRY_FIELDS:
            add("schema", ident, f"fields must be exactly {', '.join(ENTRY_FIELDS)} in that order")
        if r.get("status") not in STATUSES:
            add("schema", ident, f"status {r.get('status')!r} is not one of {', '.join(STATUSES)}")

    expected = {i for i, e in index.items() if counted(e)}
    for ident in sorted(expected - seen):
        add("missing", ident, "declared in the inventory but has no manifest record (unknown entries block release)")
    for ident in sorted(seen - set(index)):
        add("orphaned", ident, "has a record but no such declaration exists in the inventory")
    for ident in sorted(seen & {i for i, e in index.items() if not counted(e)}):
        add("import", ident, "imports are not declarations and must not have records")

    stubs = not_implemented_names(repo)
    names = (introspection or {}).get("objects", {})
    missing_artifact_reported = False
    for ident, r in sorted(records.items()):
        entry = index.get(ident)
        if entry is None or not counted(entry):
            continue
        status = r.get("status")
        if r.get("fingerprint") != fingerprint(entry):
            add("stale-signature", ident, "the declaration changed since this record was reviewed")
        if r.get("callback") != inputs.strategies.get(ident):
            add("callback", ident, f"callback strategy must be {inputs.strategies.get(ident)!r} (bin/callback-matrix)")
        if is_engine_method(entry) and entry["classification"] == "public":
            lock = inputs.locks.get(ident)
            if lock is None:
                add("engine-lock-unknown", ident, "public Engine method missing from upstream/engine-locks.json")
            elif r.get("locks_engine") != lock:
                add("engine-lock", ident, f"locks_engine must be {lock!r} (upstream/engine-locks.json)")
        elif r.get("locks_engine") is not None:
            add("engine-lock", ident, "only rack::engine::Engine methods record locks_engine")

        internal = entry["classification"] == "excluded_internal"
        if status == "excluded_internal":
            if ident not in inputs.exclusions:
                add(
                    "bogus-exclusion",
                    ident,
                    "excluded_internal but not in the reviewed allowlist bindings/exclusions.json",
                )
            if not internal:
                add(
                    "bogus-exclusion",
                    ident,
                    "the inventory classifies this as public; a difficult declaration is not internal",
                )
            if not r.get("reviewer") or not r.get("notes"):
                add("bogus-exclusion", ident, "an exclusion needs a reason and a reviewer")
        elif internal:
            add("internal-not-excluded", ident, f"the inventory classifies this as internal; it cannot be {status}")

        if r.get("callback") == "unresolved" and status not in {"unreviewed", "unrepresentable"}:
            add(
                "unresolved-callback",
                ident,
                "its callback strategy is unresolved (ADR 003); it cannot be planned or bound",
            )

        if status == "unrepresentable" and (not r.get("notes") or not r.get("reviewer")):
            add(
                "unrepresentable",
                ident,
                "unrepresentable needs the reason and the owner decision in notes, and a reviewer",
            )

        python = r.get("python")
        if python is not None:
            if not isinstance(python, dict) or set(python) - PYTHON_FIELDS:
                add("schema", ident, f"python may only have {', '.join(sorted(PYTHON_FIELDS))}")
                python = {}
            for key in ("raw", "facade"):
                value = python.get(key)
                if value is not None and not (isinstance(value, str) and DOTTED.match(value)):
                    add("schema", ident, f"python.{key} must be a dotted Python name")
            if python.get("kind") is not None and python["kind"] not in PYTHON_KINDS:
                add("schema", ident, f"python.kind {python['kind']!r} is unknown")
            if r.get("locks_engine") in {"exclusive", "shared"} and not python.get("gil_release"):
                add(
                    "gil",
                    ident,
                    "takes the engine lock, so its binding must release the GIL (python.gil_release: true)",
                )
            facade = python.get("facade")
            if facade and facade in stubs:
                add("not-implemented", ident, f"{facade} raises NotImplementedError; a stub is not an implementation")

        if status in BOUND:
            if not python or not python.get("raw") or not python.get("kind"):
                add("unbound", ident, f"{status} needs python.raw and python.kind")
            impl = r.get("implementation")
            if not impl or not (repo / impl).is_file():
                add("implementation", ident, f"{status} needs an existing implementation file, not {impl!r}")
            tests = r.get("tests") or []
            if not tests:
                add("tests", ident, f"{status} needs behavioural test node IDs")
            for node in tests:
                if not test_exists(repo, node):
                    add("tests", ident, f"test {node} does not exist")
            docs = r.get("docs") or []
            if not docs:
                add("docs", ident, f"{status} needs documentation anchors")
            for ref in docs:
                if not doc_anchor_exists(repo, ref):
                    add("docs", ident, f"documentation anchor {ref} does not exist")
            if entry["kind"] in {"alias", "alias_template"}:
                target = next((i for i, e in index.items() if e["name"] == entry.get("target") and counted(e)), None)
                if target and records.get(target, {}).get("status") not in BOUND:
                    add("alias", ident, f"an alias is complete only when its target {target} is bound")
            if introspection is None:
                if not missing_artifact_reported:
                    add(
                        "missing-runtime-artifact",
                        "-",
                        "records claim bound Python names but no introspection of the built package was given",
                    )
                    missing_artifact_reported = True
            elif python:
                for key in ("raw", "facade"):
                    value = python.get(key)
                    if value and value not in names:
                        add("not-exported", ident, f"python.{key} {value} is not in the built package")
                    elif value and names[value].get("not_implemented"):
                        add("not-implemented", ident, f"{value} reports itself as not implemented")
        if status == "verified" and not r.get("reviewer"):
            add("reviewer", ident, "verified needs a reviewer")

    for ident, ex in sorted(inputs.exclusions.items()):
        entry = index.get(ident)
        if entry is None:
            add("bogus-exclusion", ident, "the allowlist names a declaration that does not exist")
        elif entry["classification"] != "excluded_internal" or not entry.get("evidence"):
            add("bogus-exclusion", ident, "the allowlist excludes a declaration the inventory does not mark internal")
        if not ex.get("reviewer") or not ex.get("reason"):
            add("bogus-exclusion", ident, "allowlist entries need a reason and a reviewer")
    return sorted(problems)


# --- reports ---------------------------------------------------------------------


def report(inputs: Inputs, manifest: dict[str, Any]) -> dict[str, Any]:
    """Counts by family, platform and status, with the entries that most need attention listed."""
    index = inputs.index
    records = {r["id"]: r for r in manifest["entries"]}
    families: dict[str, dict[str, Counter[str]]] = defaultdict(lambda: defaultdict(Counter))
    totals: dict[str, Counter[str]] = defaultdict(Counter)
    unknown = sorted(i for i, e in index.items() if counted(e) and i not in records)
    unrepresentable = sorted(i for i, r in records.items() if r["status"] == "unrepresentable")
    unresolved = sorted(i for i, r in records.items() if r["callback"] == "unresolved")
    for ident, entry in sorted(index.items()):
        if not counted(entry):
            continue
        status = records.get(ident, {}).get("status", "unknown")
        for platform, available in availability(inputs.inventory, entry).items():
            key = status if available == "yes" else ("absent" if available == "no" else "not inventoried")
            families[family_of(inputs.inventory, entry)][platform][key] += 1
            totals[platform][key] += 1
    return {
        "inventory_sha256": inputs.inventory_sha256,
        "families": {f: {p: dict(c) for p, c in sorted(v.items())} for f, v in sorted(families.items())},
        "totals": {p: dict(c) for p, c in sorted(totals.items())},
        "unknown": unknown,
        "unrepresentable": unrepresentable,
        "unresolved_callbacks": unresolved,
    }


def release_problems(
    inputs: Inputs, manifest: dict[str, Any], platform: str, introspection: dict[str, Any] | None
) -> list[Problem]:
    """The release bar for one platform: zero records outside FINAL, a real inventory and a real artifact."""
    if platform not in PLATFORMS:
        raise ManifestError(f"unknown platform {platform!r}; choose from {', '.join(PLATFORMS)}")
    problems = validate(inputs, manifest, introspection)
    if introspection is None:
        problems.append(
            Problem("missing-runtime-artifact", "-", "a release check needs the built package's introspection")
        )
    records = {r["id"]: r for r in manifest["entries"]}
    for ident, entry in sorted(inputs.index.items()):
        if not counted(entry):
            continue
        available = availability(inputs.inventory, entry)[platform]
        if available == "not inventoried":
            problems.append(Problem("not-inventoried", platform, f"no inventory has been extracted for {platform}"))
            break
        if available == "no":
            continue
        status = records.get(ident, {}).get("status", "unknown")
        if status not in FINAL:
            problems.append(Problem("incomplete", ident, f"{status} on {platform}"))
    return sorted(set(problems))


def markdown_report(rep: dict[str, Any]) -> str:
    """docs/reference/coverage.md."""
    order = [*STATUSES, "unknown", "absent", "not inventoried"]
    lines = [
        "# Binding coverage",
        "",
        "**Generated by `bin/generate_bindings.py generate` from `bindings/manifest.json`; do not edit by hand.**",
        "",
        "Each counted declaration in the [SDK inventory](surface-schema.md), by family and platform. A",
        "full-coverage release needs every available declaration `verified` or `excluded_internal`",
        "([release bar](../coverage.md#release-bar)). `not inventoried` means no inventory has been",
        "extracted for that platform yet, which blocks a release there.",
        "",
        "## Totals",
        "",
        "| Platform | " + " | ".join(order) + " |",
        "| --- |" + " --- |" * len(order),
    ]
    for platform, counts in rep["totals"].items():
        lines.append(f"| {platform} | " + " | ".join(str(counts.get(s, 0)) for s in order) + " |")
    lines += ["", "## Needs attention", ""]
    lines.append(f"- **Unknown** (in the inventory, not in the manifest): {len(rep['unknown'])}")
    lines.append(f"- **Unrepresentable** (awaiting an owner decision): {len(rep['unrepresentable'])}")
    lines.append(f"- **Unresolved callback strategy** (ADR 003): {len(rep['unresolved_callbacks'])}")
    for title, items in (("Unknown", rep["unknown"]), ("Unrepresentable", rep["unrepresentable"])):
        if items:
            lines += ["", f"### {title}", ""] + [f"- `{i}`" for i in items]
    lines += [
        "",
        "## By family (lin-x64)",
        "",
        "| Family | " + " | ".join(order) + " |",
        "| --- |" + " --- |" * len(order),
    ]
    for family, platforms in rep["families"].items():
        counts = platforms.get("lin-x64", {})
        lines.append(f"| {family} | " + " | ".join(str(counts.get(s, 0)) for s in order) + " |")
    return "\n".join(lines) + "\n"
