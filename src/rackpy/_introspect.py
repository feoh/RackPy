# SPDX-License-Identifier: GPL-3.0-or-later
"""What the built package actually exports, for bin/check_coverage.py. Internal; runs inside Rack.

The coverage check needs evidence from the shipped package, not from source:
a manifest record that claims a Python name passes only if that name is here.
"""

import importlib
import inspect
import warnings
from types import ModuleType
from typing import Any

import _rackpy_host

import rackpy

MODULES = (
    "rackpy",
    "rackpy.errors",
    "rackpy.runtime",
    "rackpy.raw",
    "rackpy.math",
    "rackpy.types",
    "rackpy.engine",
    "rackpy.engine.components",
    "rackpy.engine.expanders",
    "rackpy.engine.host",
    "rackpy.engine.module",
    "rackpy.widget",
    "rackpy.drawing",
    "rackpy.plugin",
    "rackpy.assets",
    "rackpy.audio",
    "rackpy.midi",
    "rackpy.services",
    "rackpy.services.network",
    "rackpy.services.system",
    "rackpy.dsp",
    "rackpy.dsp.graph",
    "rackpy.dsp.graph_codegen",
    "rackpy.dsp.graph_reference",
    "rackpy.dsp.kernel",
    "rackpy.dsp.basic",
    "rackpy.dsp.digital",
    "rackpy.dsp.filters",
    "rackpy.dsp.midi",
    "rackpy.dsp.fft",
    "rackpy.dsp.resampling",
    "rackpy.dsp.ringbuffer",
    "rackpy.simd",
    "rackpy.laboratory",
)
# Protocol methods a binding can map a C++ operator or lifetime to; recorded when a class defines them itself.
DUNDERS = frozenset(
    {
        "__init__",
        "__getitem__",
        "__setitem__",
        "__pos__",
        "__neg__",
        "__add__",
        "__sub__",
        "__mul__",
        "__rmul__",
        "__truediv__",
        "__radd__",
        "__rsub__",
        "__rtruediv__",
        "__and__",
        "__rand__",
        "__or__",
        "__ror__",
        "__xor__",
        "__rxor__",
        "__invert__",
        "__lshift__",
        "__rshift__",
        "__le__",
        "__gt__",
        "__ge__",
        "__iand__",
        "__ior__",
        "__ixor__",
        "__iadd__",
        "__isub__",
        "__imul__",
        "__itruediv__",
        "__eq__",
        "__ne__",
        "__iter__",
        "__len__",
        "__lt__",
        "__call__",
        "__hash__",
        "__enter__",
        "__exit__",
        "__str__",
        "__repr__",
        "__copy__",
        "__bool__",
        "__int__",
        "__deepcopy__",
        "__init_subclass__",
    }
)


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
            protocol = inspect.isclass(obj) and name in DUNDERS
            if (name.startswith("_") and not protocol) or (public is not None and name not in public):
                continue
            if isinstance(value, ModuleType) and not value.__name__.startswith(("rackpy", "_rackpy_host")):
                continue  # imported library modules
            queue.append((f"{path}.{name}", value, depth + 1))
        if inspect.ismodule(obj) and public is not None and "__getattr__" in vars(obj):
            # Lazily provided names (deprecated aliases that warn when used): resolve them quietly.
            for name in public:
                if name not in vars(obj):
                    with warnings.catch_warnings():
                        warnings.simplefilter("ignore", DeprecationWarning)
                        queue.append((f"{path}.{name}", getattr(obj, name), depth + 1))
        if inspect.isclass(obj):
            # Annotated instance attributes (set in __init__) are part of a class's API too.
            for name in inspect.get_annotations(obj):
                if not name.startswith("_"):
                    objects.setdefault(f"{path}.{name}", {"kind": "attribute", "doc": "", "not_implemented": False})
    return {
        "schema_version": 1,
        "rackpy_version": rackpy.__version__,
        "rack_version": _rackpy_host.rack_version(),
        "binding": _rackpy_host.BINDING,
        "objects": dict(sorted(objects.items())),
    }
