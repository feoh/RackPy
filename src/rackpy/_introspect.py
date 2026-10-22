# SPDX-License-Identifier: GPL-3.0-or-later
"""What the built package actually exports, for bin/check_coverage.py. Internal; runs inside Rack.

The coverage check needs evidence from the shipped package, not from source:
a manifest record that claims a Python name passes only if that name is here.
"""

import importlib
import inspect
from types import ModuleType
from typing import Any

import _rackpy_host

import rackpy

MODULES = ("rackpy", "rackpy.errors", "rackpy.runtime", "rackpy.raw")


def kind(obj: object) -> str:
    if inspect.ismodule(obj):
        return "module"
    if inspect.isclass(obj):
        return "enum" if any(getattr(c, "__name__", "") == "Enum" for c in obj.__mro__) else "class"
    if isinstance(obj, property) or type(obj).__name__ == "property":
        return "property"
    if inspect.isroutine(obj):
        return "function"
    return "attribute"


def describe(obj: object) -> dict[str, Any]:
    doc = inspect.getdoc(obj) or ""
    return {
        "kind": kind(obj),
        "doc": doc.splitlines()[0] if doc else "",
        "not_implemented": "not implemented" in doc.lower(),
    }


def snapshot() -> dict[str, Any]:
    """Every public name reachable from rackpy's modules, two levels deep (module.Class.member)."""
    objects: dict[str, dict[str, Any]] = {}
    queue: list[tuple[str, object, int]] = [(name, importlib.import_module(name), 0) for name in MODULES]
    while queue:
        path, obj, depth = queue.pop(0)
        if path in objects:
            continue
        objects[path] = describe(obj)
        if depth > 3 or not (inspect.ismodule(obj) or inspect.isclass(obj)):
            continue
        public = getattr(obj, "__all__", None) if inspect.ismodule(obj) else None
        for name, value in vars(obj).items():
            if name.startswith("_") or (public is not None and name not in public):
                continue
            if isinstance(value, ModuleType) and not value.__name__.startswith(("rackpy", "_rackpy_host")):
                continue  # imported library modules
            queue.append((f"{path}.{name}", value, depth + 1))
    return {
        "schema_version": 1,
        "rackpy_version": rackpy.__version__,
        "rack_version": _rackpy_host.rack_version(),
        "binding": _rackpy_host.BINDING,
        "objects": dict(sorted(objects.items())),
    }
