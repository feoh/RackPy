# SPDX-License-Identifier: GPL-3.0-or-later
"""Subclass/ownership tests executed in the actual Rack UI, not a fake host."""

import gc
import threading
import weakref
from collections.abc import Sequence

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
        assert isinstance(recovered, Child)
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


def test_children_is_a_live_read_only_view() -> None:
    root, a, b, c = Widget(), Widget(), Widget(), Widget()
    try:
        view = root.children
        assert isinstance(view, Sequence) and len(view) == 0 and not view and view == ()
        root.add_child(a)
        root.add_child(b)
        assert len(view) == 2 and view and view == (a, b) and view == root.children
        assert view[0] is a and view[-1] is b and view[0:1] == (a,) and view[::-1] == (b, a)
        assert list(reversed(view)) == [b, a] and view.index(b) == 1 and view.count(a) == 1
        assert a in view and c not in view and None not in view
        with pytest.raises(IndexError):
            view[2]
        with pytest.raises(ValueError):
            view.index(c)
        snapshot = view.copy()
        iterator = iter(view)  # walks the children present when iteration starts
        root.add_child_bottom(c)
        assert snapshot == (a, b) and list(iterator) == [a, b] and view == (c, a, b)
        assert view != [c, a, b] and repr(view).startswith("WidgetChildren(")
        with pytest.raises(TypeError):
            hash(view)
        with pytest.raises(TypeError):
            view[0] = a  # type: ignore[index]
        for edit in ("append", "insert", "remove", "pop", "clear", "sort"):
            assert not hasattr(view, edit)  # changes go through add_child and remove_child
        root.remove_child(a)
        assert view == (c, b) and a.parent is None
    finally:
        root.close()
        a.close()
    with pytest.raises(StaleHandleError):
        len(view)
    with pytest.raises(StaleHandleError):
        list(view)


def test_children_view_does_not_keep_its_parent_alive() -> None:
    parent, child = Widget(), Widget()
    parent.add_child(child)
    view = parent.children
    marker = weakref.ref(parent)
    del parent
    gc.collect()
    assert marker() is None and not child.alive
    with pytest.raises(StaleHandleError):
        view[0]


def test_children_view_main_thread_rule() -> None:
    root = Widget()
    view = root.children
    errors: list[BaseException] = []

    def worker() -> None:
        try:
            len(view)
        except BaseException as error:
            errors.append(error)

    try:
        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()
        assert len(errors) == 1 and isinstance(errors[0], ThreadError)
    finally:
        root.close()
