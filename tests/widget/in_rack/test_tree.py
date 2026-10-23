# SPDX-License-Identifier: GPL-3.0-or-later
import gc
import threading
import weakref
from collections.abc import Iterator
from pathlib import Path
from typing import Any

import _rackpy_host
import pytest

import rackpy._bootstrap
import rackpy.raw.widget
from rackpy.errors import ContextError, OwnershipError, StaleHandleError, ThreadError
from rackpy.raw.math import Rect, Vec
from rackpy.widget import WidgetNode


def test_startup_only_refusal() -> None:
    assert _rackpy_host.bound_context_generation() is None
    with pytest.raises(ContextError, match="live UI context"):
        WidgetNode.create()


@pytest.fixture
def root() -> Iterator[WidgetNode]:
    assert _rackpy_host.bound_context_generation() is not None
    node = WidgetNode.create()
    try:
        yield node
    finally:
        node.close()


def test_tree_identity_transfer_and_order(root: WidgetNode) -> None:
    a, b, c = (WidgetNode.create() for _ in range(3))
    root.add_child(a)
    root.add_child_bottom(b)
    a.add_child(c)
    assert root.parent is None
    assert root.children == (b, a)
    assert root.children[1] is a and a.parent is root and c.parent is a
    assert root.has_child(a) and not root.has_child(c)
    root.remove_child(a)
    assert [node.parent for node in (a, c)] == [None, a]
    b.add_child(a)
    assert a.parent is b and c.parent is a
    root.clear_children()
    assert not a.alive and not b.alive and not c.alive
    for node in (a, b, c):
        with pytest.raises(StaleHandleError):
            node.get_box()


def test_checked_sibling_insertion_order_and_errors(root: WidgetNode) -> None:
    a, b, above, below, outsider = (WidgetNode.create() for _ in range(5))
    root.add_child(a)
    root.add_child(b)
    root.add_child_above(above, a)
    root.add_child_below(below, b)
    children = root.children
    assert children.index(a) + 1 == children.index(above)
    assert children.index(below) + 1 == children.index(b)
    assert a.parent is b.parent is above.parent is below.parent is root
    with pytest.raises(OwnershipError):
        root.add_child_above(outsider, outsider)
    with pytest.raises(OwnershipError):
        root.add_child_below(above, b)  # already attached child
    other = WidgetNode.create()
    with pytest.raises(OwnershipError):
        root.add_child_above(outsider, other)  # sibling is not a direct child
    with pytest.raises(TypeError):
        root.add_child_below(outsider, None)  # type: ignore[arg-type]
    with pytest.raises(ContextError):
        _rackpy_host._testing.with_foreign_context(lambda: root.add_child_above(outsider, a))
    stale = WidgetNode.create()
    root.add_child(stale)
    root.remove_child(stale)
    stale.close()
    with pytest.raises(StaleHandleError):
        root.add_child_below(outsider, stale)
    assert outsider.parent is None and root.children == children
    outsider.close()
    other.close()


def test_invalid_mutations_are_atomic(root: WidgetNode) -> None:
    child, orphan = WidgetNode.create(), WidgetNode.create()
    root.add_child(child)
    for parent, item in ((root, child), (root, root), (child, root)):
        with pytest.raises(OwnershipError):
            parent.add_child(item)
        assert root.children == (child,) and child.parent is root and root.parent is None
    with pytest.raises(OwnershipError):
        child.close()
    with pytest.raises(OwnershipError):
        root.remove_child(orphan)
    with pytest.raises(TypeError):
        root.add_child(None)  # type: ignore[arg-type]
    with pytest.raises(TypeError):
        root.set_box("not a rectangle")  # type: ignore[arg-type]
    assert root.children == (child,)
    orphan.close()
    orphan.close()
    with pytest.raises(StaleHandleError):
        root.add_child(orphan)


def test_snapshot_box_copy_and_layout(root: WidgetNode) -> None:
    a, b = WidgetNode.create(), WidgetNode.create()
    a.set_box(Rect(2, 3, 4, 5))
    b.set_box(Rect(-1, -2, 8, 9))
    root.add_child(a)
    snapshot = root.children
    root.add_child_bottom(b)
    assert snapshot == (a,)
    assert root.get_children_bounding_box() == Rect(-1, -2, 8, 10)
    b.set_visible(False)
    assert not b.is_visible()
    assert root.get_visible_children_bounding_box() == a.get_box()
    copy = a.get_box()
    copy.pos.x = 99
    assert a.get_box().pos.x == 2
    root.clear_children()
    assert not snapshot[0].alive


def test_geometry_visibility_and_coordinate_queries(root: WidgetNode) -> None:
    parent, child = (WidgetNode.create() for _ in range(2))
    root.set_position(Vec(10, 20))
    root.set_size(Vec(100, 80))
    parent.set_position(Vec(5, 6))
    parent.set_size(Vec(20, 15))
    child.set_position(Vec(2, 3))
    child.set_size(Vec(4, 5))
    root.add_child(parent)
    parent.add_child(child)
    assert parent.get_position() == Vec(5, 6) and parent.get_size() == Vec(20, 15)
    assert child.is_descendant_of(parent) and child.is_descendant_of(root)
    assert parent.is_descendant_of(None) is False
    assert child.get_relative_offset(Vec(1, 2), parent) == Vec(3, 5)
    assert child.get_relative_offset(Vec(1, 2), root) == Vec(8, 11)
    assert child.get_absolute_offset(Vec(1, 2)) == Vec(18, 31)
    assert child.get_relative_offset(Vec(1, 2), None) == child.get_absolute_offset(Vec(1, 2))
    assert child.get_relative_zoom(parent) == child.get_absolute_zoom() == 1
    assert child.get_relative_zoom(None) == 1
    assert child.get_viewport() == child.get_viewport(Rect.inf())
    visible_clip = child.get_viewport(Rect(0, 0, 100, 100))
    assert visible_clip.size.x >= 0 and visible_clip.size.y >= 0
    child.hide()
    assert not child.is_visible()
    child.show()
    assert child.is_visible()
    assert child.visible is True
    child.visible = False
    assert child.visible is False
    assert not child.is_visible()
    child.set_visible(True)
    assert child.is_visible()
    parent.set_position(Vec(6, 7))
    parent.set_size(Vec(30, 25))
    assert parent.get_box() == Rect(6, 7, 30, 25)
    with pytest.raises(TypeError):
        child.get_relative_offset(Vec(), object())  # type: ignore[arg-type]
    detached = WidgetNode.create()
    assert child.get_relative_zoom(detached) == 1
    detached.close()
    with pytest.raises(ContextError):
        _rackpy_host._testing.with_foreign_context(lambda: child.get_relative_offset(Vec(), root))
    stale = WidgetNode.create()
    parent.add_child(stale)
    parent.remove_child(stale)
    stale.close()
    with pytest.raises(StaleHandleError):
        child.is_descendant_of(stale)


def test_wrong_context_and_readonly_ownership(root: WidgetNode) -> None:
    with pytest.raises(ContextError):
        _rackpy_host._testing.with_foreign_context(root.get_box)
    for name, value in (("parent", None), ("children", ()), ("alive", False)):
        with pytest.raises(AttributeError):
            setattr(root, name, value)
    assert root.alive and root.parent is None and root.children == ()


def test_native_child_survives_wrapper_gc(root: WidgetNode) -> None:
    child = WidgetNode.create()
    root.add_child(child)
    marker = weakref.ref(child)
    del child
    gc.collect()
    assert marker() is None
    recovered = root.children[0]
    assert recovered.alive and recovered.parent is root
    assert root.children[0] is recovered


def test_parent_collection_invalidates_borrowed_children(root: WidgetNode) -> None:
    initial_roots = rackpy.raw.widget._native_root_count()
    for _ in range(1000):
        parent, child = WidgetNode.create(), WidgetNode.create()
        parent.add_child(child)
        snapshot = parent.children
        weak = weakref.ref(parent)
        del parent
        gc.collect()
        assert weak() is None
        assert not child.alive and not snapshot[0].alive
    assert rackpy.raw.widget._native_root_count() == initial_roots


def test_wrong_thread_and_worker_final_release(root: WidgetNode) -> None:
    errors: list[BaseException] = []

    def use() -> None:
        for action in (root.get_box, root.clear_children, root.close, WidgetNode.create, lambda: root.alive):
            try:
                action()
            except BaseException as exc:
                errors.append(exc)

    thread = threading.Thread(target=use)
    thread.start()
    thread.join()
    assert len(errors) == 5 and all(isinstance(exc, ThreadError) for exc in errors)
    owner, child = WidgetNode.create(), WidgetNode.create()
    owner.add_child(child)
    box = [owner]
    marker = weakref.ref(owner)
    del owner
    thread = threading.Thread(target=box.clear)
    thread.start()
    thread.join()
    gc.collect()
    assert marker() is None
    assert not child.alive


def test_cleanup_of_retained_tree_at_actual_host_shutdown(root: WidgetNode) -> None:
    # Hold a real subtree through Context shutdown and check native ownership
    # bookkeeping at plugin destroy, not merely the context token's liveness.
    retained = WidgetNode.create()
    child = WidgetNode.create()
    retained.add_child(child)
    original_stop = rackpy._bootstrap.stop

    def stop() -> None:
        original_stop()
        assert not retained.alive and not child.alive
        count = rackpy.raw.widget._native_root_count
        assert count() == 0
        _rackpy_host.log_info("detached-widget-cleanup-ok")

    rackpy._bootstrap.stop = stop


def test_final_adapter_cannot_hide_python_overrides() -> None:
    with pytest.raises(TypeError):
        type("Unsafe", (WidgetNode,), {"step": lambda self: None})


def test_introspection_and_runnable_snippet(root: WidgetNode) -> None:
    from rackpy._introspect import snapshot

    objects = snapshot()["objects"]
    assert "rackpy.raw.widget.WidgetNode.add_child" in objects
    assert "rackpy.widget.WidgetNode" in objects
    path = Path(__file__).resolve().parents[3] / "docs" / "snippets" / "detached_tree.py"
    scope: dict[str, Any] = {}
    exec(compile(path.read_text(), str(path), "exec"), scope)
    scope["exercise_tree"]()
