# SPDX-License-Identifier: GPL-3.0-or-later
"""Sphinx extension for the RackPy site (RP-09).

- Rewrites Markdown links that leave docs/ (to source files, tests, JSON pins)
  into links to the repository at the commit being built, and reports any
  that point at nothing.
- ``rackpy-example``: shows a snippet file from docs/snippets/ exactly as tests
  run it, so the page and the tested code cannot drift.
- ``rackpy-conceptual``: shows code that is not runnable, with a visible label.
- Any other Python code block is an error: every Python listing on the site is
  either tested or labelled.
- ``rackpy-surface``: a table of SDK declarations for one family, generated
  from upstream/api-surface.json and labelled as not yet bound.
- Images must have alt text.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import tomllib
from dataclasses import dataclass
from functools import cache
from pathlib import Path, PurePosixPath
from typing import Any, ClassVar

from docutils import nodes
from docutils.parsers.rst import directives
from sphinx.application import Sphinx
from sphinx.util import logging
from sphinx.util.docutils import SphinxDirective

DOCS = Path(__file__).resolve().parents[1]
REPO = DOCS.parent
GITHUB = "https://github.com/feoh/RackPy"
SNIPPETS = DOCS / "snippets"
PYTHON_LANGUAGES = {"python", "python3", "py", "pycon", "ipython"}
LABELLED = {"rackpy-example", "rackpy-conceptual"}

logger = logging.getLogger(__name__)


# --- versions ------------------------------------------------------------------


def repo_facts(repo: Path = REPO) -> dict[str, str]:
    """Versions the site states, read from the pinned files rather than typed into prose."""
    project = tomllib.loads((repo / "pyproject.toml").read_text(encoding="utf-8"))["project"]
    versions = json.loads((repo / "upstream" / "versions.json").read_text(encoding="utf-8"))
    runtime = json.loads((repo / "packaging" / "inputs.json").read_text(encoding="utf-8"))["python_runtime"]
    lock = tomllib.loads((repo / "uv.lock").read_text(encoding="utf-8"))
    pybind11 = next(p["version"] for p in lock["package"] if p["name"] == "pybind11")
    (minor,) = runtime
    return {
        "rackpy_version": project["version"],
        "rack_version": versions["rack"]["version"],
        "rack_commit": versions["rack"]["commit"],
        "python_version": runtime[minor]["version"],
        "python_build": f"python-build-standalone {runtime[minor]['release']}",
        "pybind11_version": pybind11,
    }


@cache
def source_ref(repo: Path = REPO) -> str:
    """The commit to link repository files at; `main` when git cannot say."""
    try:
        out = subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"], capture_output=True, text=True, check=True)
    except OSError, subprocess.CalledProcessError:
        return "main"
    return out.stdout.strip() or "main"


# --- links leaving docs/ -----------------------------------------------------------

LINK = re.compile(r"(?<!!)\[(?P<text>[^\]\n]*)\]\((?P<target>[^)\s]+)\)")
FENCE = re.compile(r"^(?P<fence>`{3,}|~{3,})")


def split_code(text: str) -> list[tuple[bool, str]]:
    """Split Markdown into (is_code, chunk) pieces: fenced blocks and inline code spans are code."""
    pieces: list[tuple[bool, str]] = []
    prose: list[str] = []
    fence: str | None = None
    block: list[str] = []
    for line in text.splitlines(keepends=True):
        m = FENCE.match(line.lstrip())
        if fence is None and m:
            pieces.extend(split_inline("".join(prose)))
            prose, fence, block = [], m["fence"], [line]
        elif fence is not None:
            block.append(line)
            stripped = line.strip()
            if stripped and set(stripped) == {fence[0]} and len(stripped) >= len(fence):
                pieces.append((True, "".join(block)))
                fence, block = None, []
        else:
            prose.append(line)
    pieces.extend(split_inline("".join(prose)))
    if block:
        pieces.append((True, "".join(block)))
    return pieces


def split_inline(text: str) -> list[tuple[bool, str]]:
    out: list[tuple[bool, str]] = []
    pos = 0
    for m in re.finditer(r"(`+)(?:.|\n)*?(?<!`)\1(?!`)", text):
        out.append((False, text[pos : m.start()]))
        out.append((True, m.group(0)))
        pos = m.end()
    out.append((False, text[pos:]))
    return [p for p in out if p[1]]


@dataclass(frozen=True)
class LinkProblem:
    target: str
    reason: str


def lexical(path: Path) -> Path:
    """Absolute and normalised without following symlinks, so copies built through symlinks behave."""
    return Path(os.path.normpath(path.absolute()))


def is_page(path: Path, docs: Path = DOCS) -> bool:
    """A Markdown or reST source that Sphinx builds into a page (not _ext/ or snippets/)."""
    docs = lexical(docs)
    if not path.is_relative_to(docs) or path.suffix not in {".md", ".rst"}:
        return False
    return path.relative_to(docs).parts[0] not in {"_ext", "snippets", "_build"}


def rewrite_links(text: str, page: Path, docs: Path = DOCS, repo: Path = REPO) -> tuple[str, list[LinkProblem]]:
    """Point links to repository files that are not site pages at GitHub; report targets that do not exist.

    Links between pages are left for MyST, which checks them and their anchors itself.
    """
    problems: list[LinkProblem] = []

    def replace(m: re.Match[str]) -> str:
        target = m["target"]
        if re.match(r"^[a-z][a-z0-9+.-]*:", target) or target.startswith("#"):
            return m.group(0)
        path, _, fragment = target.partition("#")
        resolved = lexical(page.parent / path)
        root = lexical(repo)
        if is_page(resolved, docs):
            if not resolved.exists():
                problems.append(LinkProblem(target, f"{resolved.relative_to(lexical(docs))} does not exist"))
            return m.group(0)
        if not resolved.is_relative_to(root):
            problems.append(LinkProblem(target, "points outside the repository"))
            return m.group(0)
        if not resolved.exists():
            problems.append(LinkProblem(target, f"{resolved.relative_to(root)} does not exist"))
            return m.group(0)
        rel = PurePosixPath(resolved.relative_to(root))
        kind = "tree" if resolved.is_dir() else "blob"
        url = f"{GITHUB}/{kind}/{source_ref(repo)}/{rel}" + (f"#{fragment}" if fragment else "")
        return f"[{m['text']}]({url})"

    out = []
    for is_code, chunk in split_code(text):
        out.append(chunk if is_code else LINK.sub(replace, chunk))
    return "".join(out), problems


def on_source_read(app: Sphinx, docname: str, source: list[str]) -> None:
    path = Path(app.env.doc2path(docname))
    if path.suffix != ".md":
        return
    source[0], problems = rewrite_links(source[0], path)
    for p in problems:
        logger.warning(
            "broken repository link %s: %s", p.target, p.reason, location=docname, type="rackpy", subtype="link"
        )


# --- snippets -----------------------------------------------------------------

HEADER = re.compile(r"^# rackpy-example:(?P<fields>.*)$", re.MULTILINE)
RUNS_IN = {"rack": "Runs inside Rack; tested against the built plugin", "python": "Runs with plain Python; tested"}


@dataclass(frozen=True)
class Snippet:
    path: Path
    runs_in: str
    output: Path | None

    @property
    def expected(self) -> str | None:
        return self.output.read_text(encoding="utf-8") if self.output else None


def read_snippet(path: Path) -> Snippet:
    """Parse a snippet's `# rackpy-example: runs-in=... output=...` header. Raises ValueError if malformed."""
    text = path.read_text(encoding="utf-8")
    m = HEADER.search(text)
    if not m:
        raise ValueError(f"{path.name} has no '# rackpy-example: runs-in=...' header")
    fields = dict(item.split("=", 1) for item in m["fields"].split() if "=" in item)
    runs_in = fields.get("runs-in", "")
    if runs_in not in RUNS_IN:
        raise ValueError(f"{path.name}: runs-in must be one of {sorted(RUNS_IN)}, not {runs_in!r}")
    output = path.parent / fields["output"] if "output" in fields else None
    if output and not output.is_file():
        raise ValueError(f"{path.name}: expected-output file {output.name} does not exist")
    return Snippet(path, runs_in, output)


def displayed_code(snippet: Snippet) -> str:
    """The snippet as readers see it: without the licence line and the test header."""
    lines = snippet.path.read_text(encoding="utf-8").splitlines()
    kept = [line for line in lines if not line.startswith(("# SPDX-License-Identifier:", "# rackpy-example:"))]
    while kept and not kept[0].strip():
        kept.pop(0)
    return "\n".join(kept).rstrip() + "\n"


def labelled_block(code: str, label: str, css: str) -> nodes.container:
    block = nodes.literal_block(code, code, language="python", classes=[css])
    caption = nodes.caption(label, "", nodes.Text(label))
    container = nodes.container("", caption, block, classes=["literal-block-wrapper", css], literal_block=True)
    return container


class RackPyExample(SphinxDirective):
    """Show docs/snippets/<file>, the same file the snippet tests run."""

    required_arguments = 1
    option_spec: ClassVar[dict[str, Any]] = {"caption": directives.unchanged}

    def run(self) -> list[nodes.Node]:
        path = (SNIPPETS / self.arguments[0]).resolve()
        if not path.is_relative_to(SNIPPETS) or not path.is_file():
            raise self.error(f"no snippet {self.arguments[0]} in docs/snippets/")
        try:
            snippet = read_snippet(path)
        except ValueError as e:
            raise self.error(str(e)) from e
        self.env.note_dependency(str(path))
        if snippet.output:
            self.env.note_dependency(str(snippet.output))
        caption = self.options.get("caption", path.name)
        out: list[nodes.Node] = [
            labelled_block(displayed_code(snippet), f"{caption} · {RUNS_IN[snippet.runs_in]}", "rackpy-example")
        ]
        if snippet.expected is not None:
            text = snippet.expected
            out.append(nodes.paragraph("", "", nodes.Text("Output:")))
            out.append(nodes.literal_block(text, text, language="text", classes=["rackpy-example-output"]))
        return out


class RackPyConceptual(SphinxDirective):
    """Code that is not runnable: a proposal or a sketch. Rendered with a label saying so."""

    has_content = True
    option_spec: ClassVar[dict[str, Any]] = {"caption": directives.unchanged}

    def run(self) -> list[nodes.Node]:
        code = "\n".join(self.content) + "\n"
        caption = self.options.get("caption", "Conceptual sketch")
        return [labelled_block(code, f"{caption} · not runnable, not an available API", "rackpy-conceptual")]


# --- SDK surface tables -----------------------------------------------------------


@cache
def manifest_status(repo: Path = REPO) -> dict[str, str]:
    """Each record's status in bindings/manifest.json (RP-11)."""
    doc = json.loads((repo / "bindings" / "manifest.json").read_text(encoding="utf-8"))
    return {r["id"]: r["status"] for r in doc["entries"]}


STATUS_TEXT = {
    "unreviewed": "unreviewed",
    "planned": "planned",
    "implemented": "implemented",
    "verified": "verified",
    "unrepresentable": "unrepresentable",
    "excluded_internal": "excluded (internal)",
}


@cache
def surface(repo: Path = REPO) -> dict[str, Any]:
    doc: dict[str, Any] = json.loads((repo / "upstream" / "api-surface.json").read_text(encoding="utf-8"))
    return doc


def family_entries(family: str, repo: Path = REPO) -> list[dict[str, Any]]:
    doc = surface(repo)
    families: dict[str, str] = doc["families"]
    return [e for e in doc["entries"] if families.get(e["header"]) == family and e["classification"] != "import"]


def table(headers: list[str], rows: list[list[nodes.Node]], widths: list[int]) -> nodes.table:
    tgroup = nodes.tgroup(cols=len(headers))
    for w in widths:
        tgroup += nodes.colspec(colwidth=w)
    head = nodes.thead()
    row = nodes.row()
    for h in headers:
        row += nodes.entry("", nodes.paragraph("", h))
    head += row
    tgroup += head
    body = nodes.tbody()
    for cells in rows:
        r = nodes.row()
        for cell in cells:
            r += nodes.entry("", cell)
        body += r
    tgroup += body
    return nodes.table("", tgroup, classes=["rackpy-surface"])


class RackPySurface(SphinxDirective):
    """Every inventory entry in one family, with its upstream location and RackPy status."""

    required_arguments = 1
    final_argument_whitespace = True

    def run(self) -> list[nodes.Node]:
        family = self.arguments[0]
        self.env.note_dependency(str(REPO / "upstream" / "api-surface.json"))
        self.env.note_dependency(str(REPO / "bindings" / "manifest.json"))
        statuses = manifest_status()
        entries = family_entries(family)
        if not entries:
            raise self.error(f"family {family!r} has no entries in upstream/api-surface.json")
        commit = repo_facts()["rack_commit"]
        rows: list[list[nodes.Node]] = []
        for e in entries:
            deprecated = e.get("deprecated") or e.get("comment_deprecated") or e.get("deprecated_scope")
            flags = [f for f, on in (("deprecated", deprecated), ("unstable", e.get("unstable"))) if on]
            if e.get("conditions"):
                flags.append("only if " + " and ".join(e["conditions"]))
            if e.get("absent_in"):
                flags.append("absent in " + ", ".join(e["absent_in"]))
            url = f"https://github.com/VCVRack/Rack/blob/{commit}/{e['header']}#L{e['line']}"
            where = nodes.paragraph(
                "", "", nodes.reference("", f"{e['header'].removeprefix('include/')}:{e['line']}", refuri=url)
            )
            status = STATUS_TEXT.get(statuses.get(e["id"], ""), "unknown")
            # The ID carries the signature, which tells overloads apart.
            declaration = e["id"].split(":", 1)[1]
            rows.append(
                [
                    nodes.paragraph("", "", nodes.literal(declaration, declaration)),
                    nodes.paragraph("", e["kind"].replace("_", " ")),
                    where,
                    nodes.paragraph("", "; ".join(flags)),
                    nodes.paragraph("", status),
                ]
            )
        counts = f"{len(entries)} entries. "
        bound = sum(1 for e in entries if statuses.get(e["id"]) in {"implemented", "verified"})
        note = nodes.paragraph(
            "",
            counts + f"{bound} bound. Generated from upstream/api-surface.json and bindings/manifest.json; "
            "the status column is each declaration's record in the binding manifest.",
        )
        return [note, table(["Declaration", "Kind", "Header", "Flags", "Status"], rows, [40, 10, 22, 16, 12])]


# --- checks on every page ----------------------------------------------------------


def check_doctree(app: Sphinx, doctree: nodes.document) -> None:
    for block in doctree.findall(nodes.literal_block):
        language = str(block.get("language", "")).lower()
        if language in PYTHON_LANGUAGES and not LABELLED & set(block["classes"]):
            logger.warning(
                "Python code block is neither a tested {rackpy-example} nor a labelled {rackpy-conceptual}",
                location=block,
                type="rackpy",
                subtype="snippet",
            )
    for image in doctree.findall(nodes.image):
        if not str(image.get("alt", "")).strip():
            logger.warning("image %s has no alt text", image.get("uri"), location=image, type="rackpy", subtype="a11y")


def setup(app: Sphinx) -> dict[str, Any]:
    app.connect("source-read", on_source_read)
    app.connect("doctree-read", check_doctree)
    app.add_directive("rackpy-example", RackPyExample)
    app.add_directive("rackpy-conceptual", RackPyConceptual)
    app.add_directive("rackpy-surface", RackPySurface)
    return {"version": "1", "parallel_read_safe": True, "parallel_write_safe": True}
