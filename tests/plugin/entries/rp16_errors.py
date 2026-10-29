# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-16A test entry module: registration errors that must leave nothing half-registered."""

import json
from collections.abc import Callable

import _rackpy_host

from rackpy.plugin import plugin
from rackpy.raw import plugin as raw_plugin

errors: dict[str, object] = {}


def record(name: str, action: Callable[[], object]) -> None:
    try:
        action()
    except Exception as error:
        errors[name] = type(error).__name__


plugin.add_model("PyAlpha", lambda m: m.config(0, 0, 0))
record("duplicate", lambda: plugin.add_model("PyAlpha", lambda m: None))
record("invalid", lambda: plugin.add_model("bad slug", lambda m: None))
record("empty", lambda: plugin.add_model("", lambda m: None))
record("not_callable", lambda: plugin.add_model("PyNotCallable", 42))  # type: ignore[arg-type]
model = raw_plugin.create_model("PyTwice", lambda m: None)
errors["added_twice"] = None
try:
    plugin.raw.add_model(model)
    record("staged_module", model.create_module)
    record("staged_widget", model.create_module_widget)
    plugin.raw.add_model(model)
except Exception as error:
    errors["added_twice"] = type(error).__name__
errors["models"] = [m.slug for m in plugin.raw.models]
_rackpy_host.log_info("rp16-errors " + json.dumps(errors))
