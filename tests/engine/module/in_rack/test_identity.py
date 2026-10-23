# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-14C1: module identity, the bypass query and Rack's deprecated counting constructor."""

import threading
import warnings
from collections.abc import Callable
from typing import Any

import pytest

from rackpy.errors import OwnershipError, StaleHandleError, ThreadError
from rackpy.raw.engine import Module


def test_identity_and_bypass_match_native_reference(reference: dict[str, Any]) -> None:
    expected = reference["identity"]
    seen: list[int] = []

    def configure(m: Module) -> None:
        seen.append(m.id)
        m.id = expected["assigned_id"]
        assert m.get_id() == m.id == expected["assigned_id"]
        m.id = -1  # unassigned again: an engine would pick one

    m = Module.create(configure)
    try:
        assert seen == [expected["default_id"]] and m.get_id() == expected["default_id"]
        assert m.is_bypassed() is expected["bypassed"]
    finally:
        m.close()


def test_id_is_constructor_scoped_int64() -> None:
    def configure(m: Module) -> None:
        for value in (0, 2**63 - 1, -(2**63)):
            m.id = value
            assert m.get_id() == value
        for value in (2**63, -(2**63) - 1):
            with pytest.raises(TypeError):
                m.id = value
        with pytest.raises(TypeError):
            m.id = "7"  # type: ignore[assignment]
        m.id = 42

    m = Module.create(configure)
    try:
        assert m.id == 42
        with pytest.raises(OwnershipError, match="constructor callback"):
            m.id = 43
        assert m.id == 42
        errors: list[BaseException] = []
        actions: list[Callable[[], object]] = [lambda: m.id, m.get_id, m.is_bypassed]

        def worker() -> None:
            for action in actions:
                try:
                    action()
                except BaseException as error:
                    errors.append(error)

        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()
        assert len(errors) == len(actions) and all(isinstance(e, ThreadError) for e in errors)
    finally:
        m.close()
    for action in actions:
        with pytest.raises(StaleHandleError):
            action()


def test_deprecated_counting_constructor(reference: dict[str, Any]) -> None:
    calls: list[list[int]] = []

    def configure(m: Module) -> None:
        calls.append([m.get_num_params(), m.get_num_inputs(), m.get_num_outputs(), m.get_num_lights()])
        with pytest.raises(OwnershipError, match="only once"):
            m.config(0, 0, 0)
        m.config_param(0, 0, 10, 5, "Configured afterwards")

    with pytest.warns(DeprecationWarning, match="deprecated in Rack"):
        counted = Module.create(2, 3, 4, 5, configure)
    with pytest.warns(DeprecationWarning):
        no_lights = Module.create(num_params=1, num_inputs=1, num_outputs=1)
    try:
        assert calls == [reference["identity"]["counted"]]
        assert counted.params[0].value == 5
        counts = [no_lights.get_num_params(), no_lights.get_num_inputs(), no_lights.get_num_outputs()]
        assert [*counts, no_lights.get_num_lights()] == reference["identity"]["no_lights"]
    finally:
        counted.close()
        no_lights.close()

    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)
        with pytest.raises(ValueError, match="nonnegative"):
            Module.create(-1, 0, 0)
        escaped: list[Module] = []

        def failing(m: Module) -> None:
            escaped.append(m)
            raise RuntimeError("after counting")

        with pytest.raises(RuntimeError, match="after counting"):
            Module.create(1, 1, 1, 1, failing)
        assert not escaped[0].alive
    with warnings.catch_warnings():
        warnings.simplefilter("error", DeprecationWarning)
        with pytest.raises(DeprecationWarning):
            Module.create(1, 0, 0)
    with pytest.raises(TypeError, match="callable"):
        Module.create(7)  # type: ignore[call-overload]
