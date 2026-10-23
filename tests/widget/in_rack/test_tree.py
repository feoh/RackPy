# SPDX-License-Identifier: GPL-3.0-or-later
import copy
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
from rackpy._introspect import snapshot
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


def test_native_step_deletes_requested_children_and_invalidates_views(root: WidgetNode) -> None:
    child, grandchild, keep = (WidgetNode.create() for _ in range(3))
    root.add_child(child)
    root.add_child(keep)
    child.add_child(grandchild)
    box, vector = grandchild.box, child.box.pos
    assert not bool(child.requested_delete)
    child.request_delete()
    assert bool(child.requested_delete)
    assert bool(child.alive) and bool(grandchild.alive) and list(root.children) == [child, keep]
    root.step()
    assert list(root.children) == [keep]
    assert not bool(child.alive) and not bool(grandchild.alive)
    for action in (box.copy, vector.copy, child.request_delete, child.step, lambda: child.requested_delete):
        with pytest.raises(StaleHandleError):
            action()
    root.request_delete()
    root.step()  # only a parent processes a widget's own request
    assert root.alive and bool(root.requested_delete)
    root.requested_delete = False
    keep.request_delete()
    keep.requested_delete = False  # raw field can cancel the pending request
    root.step()
    assert bool(keep.alive) and list(root.children) == [keep]
    keep.requested_delete = True
    root.step()
    assert list(root.children) == [] and not bool(keep.alive)


def test_step_recurses_hidden_trees_and_preserves_pending_request_on_remove(root: WidgetNode) -> None:
    parent, child = WidgetNode.create(), WidgetNode.create()
    root.add_child(parent)
    parent.add_child(child)
    parent.hide()
    child.request_delete()
    root.step()  # visibility only controls drawing, not stepping
    assert parent.alive and list(parent.children) == [] and not child.alive
    replacement = WidgetNode.create()
    parent.add_child(replacement)
    replacement.request_delete()
    parent.remove_child(replacement)
    root.step()
    assert bool(replacement.alive) and bool(replacement.requested_delete)
    root.add_child(replacement)
    root.step()
    assert list(root.children) == [parent] and not bool(replacement.alive)
    with pytest.raises(ContextError):
        _rackpy_host._testing.with_foreign_context(root.step)
    with pytest.raises(ContextError):
        _rackpy_host._testing.with_foreign_context(root.request_delete)
    errors: list[BaseException] = []

    def use() -> None:
        for action in (root.step, root.request_delete, lambda: setattr(root, "requested_delete", True)):
            try:
                action()
            except BaseException as exc:
                errors.append(exc)

    thread = threading.Thread(target=use)
    thread.start()
    thread.join()
    assert len(errors) == 3 and all(isinstance(exc, ThreadError) for exc in errors)


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


def test_raw_box_live_aliases_and_value_copies(root: WidgetNode) -> None:
    box = root.box
    pos, size = box.pos, box.size
    root.box = Rect(1, 2, 3, 4)
    assert pos.copy() == Vec(1, 2) and size.copy() == Vec(3, 4)
    pos.x = 10
    pos[1] = 20
    size.y = 40
    assert root.get_box() == Rect(10, 20, 3, 40)
    box.pos += Vec(2, 3)
    box.size -= Vec(1, 2)
    assert root.get_box() == Rect(12, 23, 2, 38)
    box.pos = box.size
    root.box = box  # aliasing assignment copies before writing
    assert pos.copy() == size.copy() == Vec(2, 38)
    assert pos + Vec(1, 2) == Vec(3, 40)
    assert size - pos == Vec()
    root.set_box(Rect(4, 5, 6, 7))
    assert box.copy() == Rect(4, 5, 6, 7)
    assert pos.copy() == Vec(4, 5)
    independent = box.copy()
    independent.pos.x = 999
    assert root.get_position() == Vec(4, 5)
    assert copy.copy(box) == copy.deepcopy(box) == box.copy()
    assert copy.copy(pos) == copy.deepcopy(pos) == pos.copy()
    for index in (-1, 2):
        with pytest.raises(IndexError):
            _ = pos[index]
        with pytest.raises(IndexError):
            pos[index] = 7
    before = root.get_box()
    with pytest.raises(TypeError):
        root.box = "not a Rect"  # type: ignore[assignment]
    with pytest.raises(TypeError):
        box.pos = (1, 2)  # type: ignore[assignment]
    with pytest.raises(TypeError):
        pos.x = "not a float"  # type: ignore[assignment]
    assert root.get_box() == before
    assert "rackpy.raw.widget.WidgetNode.box" in snapshot()["objects"]


def test_box_views_survive_attached_wrapper_but_not_native_owner(root: WidgetNode) -> None:
    child = WidgetNode.create()
    root.add_child(child)
    box, pos = child.box, child.box.pos
    marker = weakref.ref(child)
    del child
    gc.collect()
    assert marker() is None
    pos.x = 7
    assert root.children[0].get_position().x == 7
    root.clear_children()
    for action in (box.copy, pos.copy, lambda: box.pos, lambda: setattr(pos, "x", 8)):
        with pytest.raises(StaleHandleError):
            action()
    owner = WidgetNode.create()
    orphaned_view = owner.box
    weak_owner = weakref.ref(owner)
    del owner
    gc.collect()
    assert weak_owner() is None
    with pytest.raises(StaleHandleError):
        orphaned_view.copy()


def test_box_view_context_thread_and_source_validation(root: WidgetNode) -> None:
    box, pos = root.box, root.box.pos
    with pytest.raises(ContextError):
        _rackpy_host._testing.with_foreign_context(box.copy)
    with pytest.raises(ContextError):
        _rackpy_host._testing.with_foreign_context(lambda: setattr(pos, "y", 3))
    errors: list[BaseException] = []

    def use() -> None:
        for action in (box.copy, pos.copy, lambda: setattr(pos, "x", 9), lambda: setattr(box, "pos", Vec())):
            try:
                action()
            except BaseException as exc:
                errors.append(exc)

    thread = threading.Thread(target=use)
    thread.start()
    thread.join()
    assert len(errors) == 4 and all(isinstance(exc, ThreadError) for exc in errors)
    other = WidgetNode.create()
    stale_box, stale_pos = other.box, other.box.pos
    other.close()
    before = root.get_box()
    with pytest.raises(StaleHandleError):
        root.box = stale_box
    with pytest.raises(StaleHandleError):
        box.pos = stale_pos
    assert root.get_box() == before


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
    retained_box, retained_pos = child.box, child.box.pos
    original_stop = rackpy._bootstrap.stop

    def stop() -> None:
        original_stop()
        assert not retained.alive and not child.alive
        for action in (retained_box.copy, retained_pos.copy):
            with pytest.raises((StaleHandleError, ContextError)):
                action()
        count = rackpy.raw.widget._native_root_count
        assert count() == 0
        _rackpy_host.log_info("detached-widget-cleanup-ok")

    rackpy._bootstrap.stop = stop


def test_final_adapter_cannot_hide_python_overrides() -> None:
    with pytest.raises(TypeError):
        type("Unsafe", (WidgetNode,), {"step": lambda self: None})
    for view_type in (rackpy.raw.widget.WidgetBoxView, rackpy.raw.widget.WidgetVecView):
        with pytest.raises(TypeError):
            view_type()


def test_introspection_and_runnable_snippet(root: WidgetNode) -> None:
    objects = snapshot()["objects"]
    for member in ("add_child", "box", "step", "request_delete", "requested_delete"):
        assert f"rackpy.raw.widget.WidgetNode.{member}" in objects
    assert "rackpy.widget.WidgetNode" in objects
    path = Path(__file__).resolve().parents[3] / "docs" / "snippets" / "detached_tree.py"
    scope: dict[str, Any] = {}
    exec(compile(path.read_text(), str(path), "exec"), scope)
    scope["exercise_tree"]()
