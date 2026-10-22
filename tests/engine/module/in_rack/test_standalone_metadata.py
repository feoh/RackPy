# SPDX-License-Identifier: GPL-3.0-or-later
"""Standalone metadata keeps no module alive and never retains a dangling SDK pointer."""

import gc
import threading
import weakref
from collections.abc import Callable
from typing import Any

import pytest

from rackpy.errors import StaleHandleError, ThreadError
from rackpy.raw.engine import LightInfo, Module, Port, PortInfo


def linked_module(info: PortInfo | LightInfo) -> Module | None:
    # Native teardown mutates this property invisibly to mypy's narrowing.
    return info.module


@pytest.mark.parametrize("info_class", [PortInfo, LightInfo])
def test_standalone_module_identity_null_and_reassignment(
    info_class: type[PortInfo] | type[LightInfo], reference: dict[str, Any]
) -> None:
    info = info_class()
    first, second = Module.create(), Module.create()
    try:
        observed = [info.module is None]
        info.module = first
        observed.append(info.module is first)
        info.module = second
        observed.append(info.module is second)
        first.close()  # the old subscription must not clear the new pointer
        assert info.module is second
        info.module = None
        observed.append(info.module is None)
        assert observed == reference["standalone_backreferences"]
        # Assigning the backreference does not transfer ownership into a slot.
        assert second.get_num_inputs() == second.get_num_lights() == 0
        info.module = second
        second.close()
        assert linked_module(info) is None
        info.name, info.description = "Still owned", "Still valid"
        assert info.get_name() == "Still owned" and info.get_description() == "Still valid"
        for case in reference["metadata"]:
            info.name = case["name"]
            info.description = "A description"
            expected = case["configured"][0]
            if isinstance(info, PortInfo):
                info.port_id = 0
                info.type = Port.Type.INPUT
                assert [info.get_name(), info.get_description()] == expected[1:3]
                assert info.get_full_name() == info.get_name() + " input"
            else:
                assert [info.get_name(), info.get_description()] == expected[5:7]
    finally:
        first.close()
        second.close()


@pytest.mark.parametrize("info_class", [PortInfo, LightInfo])
def test_standalone_module_assignment_errors_are_atomic(info_class: type[PortInfo] | type[LightInfo]) -> None:
    info = info_class()
    owner, closed = Module.create(), Module.create()
    closed.close()
    try:
        info.module = owner
        with pytest.raises(TypeError):
            info.module = object()  # type: ignore[assignment]
        assert info.module is owner
        with pytest.raises(StaleHandleError):
            info.module = closed
        assert info.module is owner
        actions: list[Callable[[], object]] = [
            lambda: info.module,
            lambda: setattr(info, "module", None),
            lambda: setattr(info, "module", closed),
            lambda: setattr(info, "module", owner),
        ]
        outcomes: list[BaseException] = []

        def worker() -> None:
            for action in actions:
                try:
                    action()
                except BaseException as error:
                    outcomes.append(error)

        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()
        assert len(outcomes) == len(actions) and all(isinstance(e, ThreadError) for e in outcomes)
        assert info.module is owner
    finally:
        owner.close()


@pytest.mark.parametrize("info_class", [PortInfo, LightInfo])
def test_standalone_module_rollback_gc_and_churn(info_class: type[PortInfo] | type[LightInfo]) -> None:
    info = info_class()

    def configure(module: Module) -> None:
        info.module = module
        assert info.module is module
        raise ValueError("rollback standalone backreference")

    with pytest.raises(ValueError, match="rollback standalone"):
        Module.create(configure)
    assert linked_module(info) is None

    seen: weakref.WeakSet[Module] = weakref.WeakSet()
    survivor = Module.create()
    try:
        for _ in range(1000):
            owner = Module.create()
            seen.add(owner)
            info.module = owner
            assert info.module is owner
            del owner
            assert linked_module(info) is None
            info.module = survivor
            # Delete a linked metadata object before its module.
            temporary = info_class()
            temporary.module = survivor
            del temporary
        gc.collect()
        assert not seen and info.module is survivor
        # Last metadata reference can also be dropped on a Python worker.
        pending = [info_class()]
        pending[0].module = survivor
        thread = threading.Thread(target=pending.clear)
        thread.start()
        thread.join()
    finally:
        survivor.close()
    assert linked_module(info) is None


def test_standalone_subclass_virtuals_and_errors_preserve_backreferences() -> None:
    class Named(PortInfo):
        def get_name(self) -> str:
            assert self.module is owner
            return "Linked"

        def get_description(self) -> str:
            raise ValueError("description failed")

    class Lit(LightInfo):
        def get_name(self) -> str:
            assert self.module is owner
            return "Activity"

    owner = Module.create()
    named, light = Named(), Lit()
    named.module = light.module = owner
    try:
        assert named.get_full_name() == "Linked input"
        assert light.get_name() == "Activity"
        with pytest.raises(ValueError, match="description failed"):
            named.get_description()
        assert named.module is light.module is owner

        # Reentrant module deletion in a Python override remains safe.
        class Closing(PortInfo):
            def get_name(self) -> str:
                owner.close()
                assert self.module is None
                return "Detached"

        closing = Closing()
        closing.module = owner
        assert closing.get_full_name() == "Detached input"
        assert named.module is light.module is None
    finally:
        owner.close()
