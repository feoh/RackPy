# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixtures for the documentation tests: real site builds and throwaway copies to break."""

import shutil
import subprocess
import sys
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.support import REPO

DOCS = REPO / "docs"
sys.path.insert(0, str(DOCS / "_ext"))
# Top-level entries a docs build reads; everything else stays out of the copies.
NEEDED = ("pyproject.toml", "uv.lock", "upstream", "packaging", "src", "bindings", "README.md", "CONTRIBUTING.md")
LINKED = (
    *NEEDED,
    "examples",
    "tests",
    "bin",
    "spikes",
    "benchmarks",
    "planning",
    "native",
    "cmake",
    "LICENSE",
    "NOTICE",
    "SECURITY.md",
    ".github",
)


@dataclass(frozen=True)
class Build:
    returncode: int
    output: str
    html: Path

    def warnings(self) -> list[str]:
        return [line for line in self.output.splitlines() if "WARNING" in line or "ERROR" in line]


def sphinx(docs: Path, out: Path) -> Build:
    proc = subprocess.run(
        [sys.executable, "-m", "sphinx", "-W", "--keep-going", "-n", "-q", "-b", "html", str(docs), str(out)],
        capture_output=True,
        text=True,
        check=False,
    )
    return Build(proc.returncode, proc.stdout + proc.stderr, out)


@pytest.fixture(scope="session")
def site(tmp_path_factory: pytest.TempPathFactory) -> Build:
    """The real site, built the way CI builds it."""
    return sphinx(DOCS, tmp_path_factory.mktemp("site"))


@pytest.fixture
def broken_site(tmp_path: Path) -> Callable[[dict[str, str]], Build]:
    """Build a copy of the site with some pages added or replaced, to check that a mistake fails the build."""

    def build(pages: dict[str, str]) -> Build:
        root = tmp_path / "repo"
        root.mkdir()
        for name in LINKED:
            if (REPO / name).exists():
                (root / name).symlink_to(REPO / name)
        shutil.copytree(DOCS, root / "docs", ignore=shutil.ignore_patterns("__pycache__", "python"))
        for rel, text in pages.items():
            (root / "docs" / rel).write_text(text)
        return sphinx(root / "docs", tmp_path / "html")

    return build
