# SPDX-License-Identifier: GPL-3.0-or-later
"""Legacy Module virtuals remain independently overridable in the laboratory."""

import warnings
from typing import Any, cast

import pytest

from rackpy.laboratory import ModuleClass
from rackpy.raw.engine import Module


@pytest.mark.parametrize(
    ("name", "event"),
    [
        ("on_add", Module.AddEvent()),
        ("on_remove", Module.RemoveEvent()),
        ("on_reset", Module.ResetEvent()),
        ("on_randomize", Module.RandomizeEvent()),
        ("on_sample_rate_change", Module.SampleRateChangeEvent(48000, 1 / 48000)),
    ],
)
def test_legacy_override_and_modern_dispatch(name: str, event: Any) -> None:
    legacy = name + "_legacy"
    calls: list[str] = []

    def configure(self: ModuleClass) -> None:
        self.module.config(0, 0, 0, 0)

    def old(self: ModuleClass) -> None:
        calls.append("legacy")
        with pytest.warns(DeprecationWarning):
            getattr(ModuleClass, legacy)(self)

    with pytest.warns(DeprecationWarning, match=legacy):
        cls = cast("type[ModuleClass]", type("Legacy", (ModuleClass,), {"configure": configure, legacy: old}))
    obj = cls.create()
    with warnings.catch_warnings(record=True) as seen:
        warnings.simplefilter("always")
        getattr(obj.module, name)(event)
    assert calls == ["legacy"]
    assert not seen  # dispatch via Rack's modern event itself is not deprecated

    calls.clear()
    with pytest.warns(DeprecationWarning):
        getattr(obj.module, name)()
    assert calls == ["legacy"]

    def modern(self: ModuleClass, event: Any) -> None:
        calls.append("modern")

    with pytest.warns(DeprecationWarning, match=legacy):
        suppressed = cast(
            "type[ModuleClass]", type("Modern", (ModuleClass,), {"configure": configure, legacy: old, name: modern})
        )
    obj = suppressed.create()
    calls.clear()
    getattr(obj.module, name)(event)
    assert calls == ["modern"]  # an override that omits super suppresses the legacy virtual

    def modern_super(self: ModuleClass, event: Any) -> None:
        calls.append("modern")
        getattr(ModuleClass, name)(self, event)

    with pytest.warns(DeprecationWarning, match=legacy):
        delegated = cast(
            "type[ModuleClass]",
            type("Delegated", (ModuleClass,), {"configure": configure, legacy: old, name: modern_super}),
        )
    obj = delegated.create()
    calls.clear()
    getattr(obj.module, name)(event)
    assert calls == ["modern", "legacy"]


def test_legacy_override_error_reaches_caller() -> None:
    with pytest.warns(DeprecationWarning, match="on_add_legacy"):

        class Failure(ModuleClass):
            def configure(self) -> None:
                self.module.config(0, 0, 0, 0)

            def on_add_legacy(self) -> None:
                raise ValueError("legacy failure")

    obj = Failure.create()
    with pytest.raises(ValueError, match="legacy failure"):
        obj.module.on_add(Module.AddEvent())
