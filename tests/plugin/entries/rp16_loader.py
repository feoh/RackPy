# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-16 test entry module: Rack's plugin-loader methods while this plugin initializes."""

import json
from collections.abc import Callable

import _rackpy_host

from rackpy import raw
from rackpy.plugin import plugin

this = plugin.raw
report: dict[str, object] = {}


def outcome(name: str, action: Callable[[], object]) -> None:
    try:
        action()
        report[name] = "ok"
    except Exception as error:
        report[name] = type(error).__name__


outcome("rename", lambda: this.from_json({"name": "RackPy renamed", "description": "from Python"}))
outcome("slug", lambda: this.from_json({"slug": "Other"}))
outcome("abi", lambda: this.from_json({"name": "Half applied", "version": "1.0.0"}))
outcome("types", lambda: this.from_json({"author": 3}))
report["fields"] = [this.name, this.description, this.version, this.brand]
plugin.add_model("PyAlpha", lambda m: m.config(0, 0, 0))
outcome("modules_match", lambda: this.modules_from_json([{"slug": "PyAlpha", "name": "Alpha"}]))
outcome("modules_missing", lambda: this.modules_from_json([]))
extra = [{"slug": "PyAlpha", "name": "A"}, {"slug": "PyB", "name": "B"}]
outcome("modules_extra", lambda: this.modules_from_json(extra))
outcome("modules_nameless", lambda: this.modules_from_json([{"slug": "PyAlpha"}]))
outcome("modules_type", lambda: this.modules_from_json({"slug": "PyAlpha"}))  # type: ignore[arg-type]
report["exception_type"] = raw.Exception.__name__
_rackpy_host.log_info("rp16-loader " + json.dumps(report))
