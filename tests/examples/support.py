# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared helpers for the example tests: the examples, the plugin tool and Rack patches."""

import json
import subprocess
from pathlib import Path
from typing import Any

from tests.support import BIN, REPO

EXAMPLES = REPO / "examples"


def plugin_tool(*args: str | Path, cwd: Path | None = None, ok: bool = True) -> subprocess.CompletedProcess[str]:
    """Run bin/rackpy-plugin; with ``ok``, a failure fails the test with the tool's output."""
    proc = subprocess.run(
        [str(BIN / "rackpy-plugin"), *map(str, args)], capture_output=True, text=True, check=False, cwd=cwd
    )
    if ok:
        assert proc.returncode == 0, proc.stdout + proc.stderr
    return proc


def project_files(root: Path) -> dict[str, bytes]:
    """A project's files, by relative path, without the hand-written documentation and caches."""
    skip = {"README.md", "PROVENANCE.md"}
    return {
        p.relative_to(root).as_posix(): p.read_bytes()
        for p in sorted(root.rglob("*"))
        if p.is_file() and p.name not in skip and "__pycache__" not in p.parts
    }


def write_patch(user: Path, modules: list[dict[str, object]], cables: list[dict[str, object]]) -> None:
    """The autosave patch Rack opens at start, with cables."""
    (user / "autosave").mkdir(parents=True, exist_ok=True)
    patch = {"version": "2.6.6", "modules": modules, "cables": cables}
    (user / "autosave" / "patch.json").write_text(json.dumps(patch))
    (user / "settings.json").write_text(json.dumps({"showTipsOnLaunch": False}))


def saved_patch(user: Path) -> dict[str, Any]:
    """The autosave Rack wrote when it last exited."""
    data: dict[str, Any] = json.loads((user / "autosave" / "patch.json").read_text())
    return data
