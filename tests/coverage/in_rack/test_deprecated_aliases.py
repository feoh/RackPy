# SPDX-License-Identifier: GPL-3.0-or-later
"""Every deprecated SDK typedef is reachable and warns inside the real host."""

import json
from pathlib import Path
from typing import Any

import pytest

from rackpy import raw

REPO = Path(__file__).resolve().parents[3]


def cases() -> list[tuple[str, str, str]]:
    inventory = json.loads((REPO / "upstream/api-surface.json").read_text())
    manifest = json.loads((REPO / "bindings/manifest.json").read_text())
    recorded = {entry["id"] for entry in manifest["entries"]}
    return [
        (entry["id"], entry["name"], entry["target"])
        for entry in inventory["entries"]
        if entry["kind"] == "alias"
        and (entry.get("deprecated") or entry.get("comment_deprecated"))
        and entry["id"] in recorded
    ]


def resolve(qualified: str) -> Any:
    names = qualified.split("::")
    assert names.pop(0) == "rack"
    value: Any = raw
    for name in names:
        value = getattr(value, name)
    return value


@pytest.mark.parametrize(("entry_id", "alias", "target"), cases(), ids=lambda value: value)
def test_deprecated_alias_warns_and_preserves_identity(entry_id: str, alias: str, target: str) -> None:
    assert entry_id.startswith("alias:")
    expected = resolve(target)
    with pytest.warns(DeprecationWarning):
        assert resolve(alias) is expected
