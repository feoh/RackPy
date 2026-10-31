# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-09: the site builds with zero warnings, and each kind of mistake fails the build."""

import re
from collections.abc import Callable
from pathlib import Path

import pytest
import rackpy_docs

from tests.docs.conftest import DOCS, Build
from tests.support import REPO

pytestmark = pytest.mark.docs
INSTALLATION = (DOCS / "installation.md").read_text()


def test_site_builds_with_zero_warnings(site: Build) -> None:
    assert site.returncode == 0, site.output[-4000:]
    assert site.warnings() == []


def test_front_page_states_status_and_pinned_versions(site: Build) -> None:
    facts = rackpy_docs.repo_facts()
    html = (site.html / "index.html").read_text()
    assert "early development" in html
    assert "no release" in html
    for key in ("rackpy_version", "rack_version", "rack_commit", "python_version", "pybind11_version"):
        assert facts[key] in html, key
    assert facts["python_version"] == "3.14.9" and facts["pybind11_version"] == "3.1.0"


def test_surface_pages_list_every_inventory_entry_with_its_manifest_status(site: Build) -> None:
    doc = rackpy_docs.surface()
    families = sorted(set(doc["families"].values()))
    shown = 0
    for page in (site.html / "reference" / "surface").glob("*.html"):
        html = page.read_text()
        assert "Not bound yet" in html, page.name
        body = html.split("<tbody>", 1)[1].split("</tbody>", 1)[0]
        rows = body.count("<tr")
        shown += rows
        statuses = [f"<td><p>{s}</p></td>" for s in rackpy_docs.STATUS_TEXT.values()]
        assert rows == sum(body.count(s) for s in statuses), page.name
        assert "<td><p>unknown</p></td>" not in body, page.name
    expected = sum(len(rackpy_docs.family_entries(f)) for f in families)
    assert shown == expected == sum(1 for e in doc["entries"] if e["classification"] != "import")


def test_repository_links_point_at_existing_files(site: Build) -> None:
    html = (site.html / "compatibility.html").read_text()
    ref = re.escape(rackpy_docs.source_ref())
    links = re.findall(rf'href="https://github\.com/feoh/RackPy/(?:blob|tree)/{ref}/([^"#]+)', html)
    assert links, "compatibility.md links to repository files"
    for rel in links:
        assert (REPO / rel).exists(), rel


@pytest.mark.parametrize(
    ("addition", "message"),
    [
        ("\nSee [the missing page](no-such-page.md).\n", "no-such-page.md"),
        ("\nSee [a missing anchor](architecture.md#no-such-heading).\n", "no-such-heading"),
        ("\nSee [a missing file](../bin/no-such-tool).\n", "broken repository link ../bin/no-such-tool"),
        ("\n```python\nprint('untested')\n```\n", "neither a tested {rackpy-example} nor a labelled"),
        ("\n```{rackpy-example} no_such_snippet.py\n```\n", "no snippet no_such_snippet.py"),
        ("\n![](../LICENSE)\n", "has no alt text"),
        ("\n```{rackpy-example-source} drawing/no_such.py\n:pyobject: Meter\n```\n", "no example source"),
        (
            "\n```{rackpy-example-source} drawing/pythonpath/rackpydrawing/meter.py\n:pyobject: NoSuchClass\n```\n",
            "has no top-level class or function NoSuchClass",
        ),
    ],
    ids=[
        "page",
        "anchor",
        "repository-file",
        "unlabelled-python",
        "missing-snippet",
        "image-alt",
        "missing-example",
        "missing-object",
    ],
)
def test_mistakes_fail_the_build(broken_site: Callable[[dict[str, str]], Build], addition: str, message: str) -> None:
    build = broken_site({"installation.md": INSTALLATION + addition})
    assert build.returncode != 0
    assert any(message in w for w in build.warnings()), build.output[-3000:]


def test_conceptual_code_is_labelled_and_accepted(broken_site: Callable[[dict[str, str]], Build]) -> None:
    sketch = "\n```{rackpy-conceptual}\n:caption: Proposed\nclass Gain: ...\n```\n"
    build = broken_site({"installation.md": INSTALLATION + sketch})
    assert build.returncode == 0, build.output[-3000:]
    assert "not runnable, not an available API" in (build.html / "installation.html").read_text()


def test_link_rewriting_leaves_code_and_urls_alone(tmp_path: Path) -> None:
    page = DOCS / "decisions" / "example.md"
    text = (
        "[pins](../../upstream/versions.json) [web](https://example.org/x.md) [here](#top) "
        "[adr](002-embedding-and-bindings.md)\n"
        "`[code](../../nowhere)`\n"
        "```text\n[fenced](../../nowhere)\n```\n"
        "[gone](../../upstream/nowhere.json)\n"
    )
    out, problems = rackpy_docs.rewrite_links(text, page)
    ref = rackpy_docs.source_ref()
    assert f"[pins](https://github.com/feoh/RackPy/blob/{ref}/upstream/versions.json)" in out
    assert "[web](https://example.org/x.md)" in out and "[here](#top)" in out
    assert "[adr](002-embedding-and-bindings.md)" in out
    assert "`[code](../../nowhere)`" in out and "[fenced](../../nowhere)" in out
    assert [p.target for p in problems] == ["../../upstream/nowhere.json"]
