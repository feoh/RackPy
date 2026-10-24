# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-15D: the plugin settings hooks as Rack's loader calls them (live UI suite)."""

import math
import threading
from typing import Any

import _rackpy_host
import pytest
from _rackpy_host import _testing as HOOKS

from rackpy.errors import ThreadError
from rackpy.plugin import plugin
from rackpy.raw import plugin as rp


def raised() -> int:
    return _rackpy_host.dispatch_stats()["raised"]


def test_settings_reach_rack_and_back() -> None:
    received: list[Any] = []
    try:
        assert HOOKS.settings_to_json_as_rack() is None  # no handler: Rack keeps what it stored
        plugin.set_settings_handlers(to_json=lambda: {"theme": "dark", "n": [1, 2]}, from_json=received.append)
        assert HOOKS.settings_to_json_as_rack() == ({"theme": "dark", "n": [1, 2]},)
        HOOKS.settings_from_json_as_rack({"theme": "light"})
        HOOKS.settings_from_json_as_rack(None)  # JSON null passes as None
        assert received == [{"theme": "light"}, None]
        rp.set_settings_handlers(to_json=lambda: None)
        assert HOOKS.settings_to_json_as_rack() is None  # None keeps the stored settings
    finally:
        rp.set_settings_handlers()


def test_settings_errors_are_contained() -> None:
    def fail() -> Any:
        raise RuntimeError("settings-intentional-error")

    try:
        for to_json in (fail, lambda: {"bad": math.nan}, lambda: {"bad": "nul\x00"}):
            rp.set_settings_handlers(to_json=to_json, from_json=lambda data: 1 / 0)
            count = raised()
            assert HOOKS.settings_to_json_as_rack() is None  # Rack keeps the stored settings
            HOOKS.settings_from_json_as_rack({"x": 1})
            assert raised() == count + 2
    finally:
        rp.set_settings_handlers()


def test_settings_handlers_are_checked() -> None:
    with pytest.raises(TypeError, match="callable"):
        rp.set_settings_handlers(to_json=42)  # type: ignore[arg-type]
    errors: list[BaseException] = []

    def worker() -> None:
        try:
            rp.set_settings_handlers()
        except BaseException as error:
            errors.append(error)

    thread = threading.Thread(target=worker)
    thread.start()
    thread.join()
    assert len(errors) == 1 and isinstance(errors[0], ThreadError)
