# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from typing import Any

from rackpy.plugin import plugin

prefs: dict[str, Any] = {"schema": 1, "theme": "dark"}


def load(data: Any) -> None:
    if isinstance(data, dict) and data.get("theme") in {"dark", "light"}:
        prefs["theme"] = data["theme"]  # ignore anything else: users can edit settings.json


plugin.set_settings_handlers(to_json=lambda: dict(prefs), from_json=load)

load({"theme": "light", "unknown": True})
load({"theme": 42})
assert prefs == {"schema": 1, "theme": "light"}
