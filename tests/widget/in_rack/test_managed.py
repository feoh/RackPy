# SPDX-License-Identifier: GPL-3.0-or-later
"""Subclass/ownership tests executed in the actual Rack UI, not a fake host."""

import gc
import threading
import weakref

import pytest

from rackpy.errors import OwnershipError, StaleHandleError, ThreadError
from rackpy.raw.widget import Widget


def test_managed_subclass_native_step_and_transfer() -> None:
    calls: list[int] = []

    class Child(Widget):
        def step(self) -> None:
            calls.append(1)
            super().step()

    root, child = Widget(), Child()
    try:
        root.add_child(child)
        assert root.children == (child,) and child.parent is root
        root.step()
        assert calls == [1]
        marker = weakref.ref(child)
        del child
        gc.collect()
        assert marker() is not None  # raw Rack ownership retains subclass peer
        root.step()
        assert calls == [1, 1]
        recovered = root.children[0]
        root.remove_child(recovered)
        assert recovered.parent is None
        root.add_child(recovered)
        root.clear_children()
        assert not recovered.alive
        with pytest.raises((StaleHandleError, ValueError)):
            recovered.get_box()
        root.close()
        assert not root.alive
        root.close()
    finally:
        root.close()


def test_managed_callback_error_containment_and_worker_release() -> None:
    class Bad(Widget):
        def step(self) -> None:
            raise RuntimeError("managed-widget-intentional-error")

    root, child = Widget(), Bad()
    root.add_child(child)
    root.step()  # exception cannot escape native parent traversal
    root.close()
    errors: list[BaseException] = []
    owner = Widget()
    marker = weakref.ref(owner)
    held = [owner]
    del owner

    def drop() -> None:
        try:
            held[0].get_box()
        except BaseException as exc:
            errors.append(exc)
        held.clear()

    worker = threading.Thread(target=drop)
    worker.start()
    worker.join()
    gc.collect()
    assert len(errors) == 1 and isinstance(errors[0], ThreadError)
    assert marker() is None
    sweep = Widget()
    sweep.step()
    sweep.close()


def test_native_destructor_keeps_parent_peer_until_children_are_removed() -> None:
    seen: list[object] = []
    dying: list[object] = []
    root = Widget()

    class Child(Widget):
        def on_remove(self, event: object) -> None:
            assert self.parent is root
            assert root.get_box() == root.get_box()
            with pytest.raises(OwnershipError):
                _ = root.children
            with pytest.raises(OwnershipError):
                root.get_children_bounding_box()
            seen.append(1)

        def __del__(self) -> None:
            try:
                dying.append(self.alive)
            except Exception as exc:
                dying.append(type(exc).__name__)

    child = Child()
    root.add_child(child)
    del child
    root.close()
    assert seen == [1]
    assert dying == [False]


def test_managed_ownership_failures() -> None:
    root, child = Widget(), Widget()
    root.add_child(child)
    try:
        with pytest.raises(OwnershipError):
            child.add_child(root)
        with pytest.raises(OwnershipError):
            child.close()
        with pytest.raises(OwnershipError):
            root.add_child(child)
    finally:
        root.close()
