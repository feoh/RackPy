# SPDX-License-Identifier: GPL-3.0-or-later
"""Public Widget events, native traversal and expiring callback records."""

from typing import Any

import pytest

from rackpy.errors import OwnershipError, StaleHandleError
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec


def test_position_recursion_consumption_and_expiring_views() -> None:
    seen: list[object] = []
    retained: list[Any] = []

    class Child(w.Widget):
        def on_hover(self, event: Any) -> None:
            seen.append(event.pos)
            retained.extend([event, event.context])
            with pytest.raises(TypeError):
                event.pos = Vec(99, 99)
            event.consume(self)

    root, bottom, top = w.Widget(), Child(), Child()
    bottom.set_box(Rect(Vec(10, 20), Vec(100, 100)))
    top.set_box(Rect(Vec(10, 20), Vec(100, 100)))
    root.add_child(bottom)
    root.add_child(top)
    context = w.EventContext()
    event = w.HoverEvent(pos=Vec(13, 25), mouse_delta=Vec(2, 3), context=context)
    try:
        root.on_hover(event)
        assert len(seen) == 1 and seen[0] == Vec(3, 5)
        assert event.get_target() is top
        assert context.target is top and context.consumed and not context.propagating
        for ref in retained:
            with pytest.raises(StaleHandleError):
                _ = ref.context if isinstance(ref, w.BaseEvent) else ref.target
        assert event.pos == Vec(13, 25)  # caller-owned data remains valid
        event.unconsume()
        assert not event.is_consumed() and not event.is_propagating()
        context.propagating = True
        root.remove_child(top)
        top.close()
        with pytest.raises(StaleHandleError):
            event.get_target()
        context.target = None
        assert event.get_target() is None
    finally:
        root.close()


def test_all_event_handlers_and_prototype_hierarchy() -> None:
    calls: list[str] = []
    saved: list[Any] = []
    names = [
        ("DoubleClickEvent", "on_double_click"),
        ("EnterEvent", "on_enter"),
        ("LeaveEvent", "on_leave"),
        ("SelectEvent", "on_select"),
        ("DeselectEvent", "on_deselect"),
        ("SelectKeyEvent", "on_select_key"),
        ("SelectTextEvent", "on_select_text"),
        ("DragStartEvent", "on_drag_start"),
        ("DragEndEvent", "on_drag_end"),
        ("DragMoveEvent", "on_drag_move"),
        ("DragEnterEvent", "on_drag_enter"),
        ("DragLeaveEvent", "on_drag_leave"),
        ("DragDropEvent", "on_drag_drop"),
        ("ActionEvent", "on_action"),
        ("ChangeEvent", "on_change"),
        ("DirtyEvent", "on_dirty"),
        ("RepositionEvent", "on_reposition"),
        ("ResizeEvent", "on_resize"),
        ("AddEvent", "on_add"),
        ("RemoveEvent", "on_remove"),
        ("ShowEvent", "on_show"),
        ("HideEvent", "on_hide"),
        ("ContextCreateEvent", "on_context_create"),
        ("ContextDestroyEvent", "on_context_destroy"),
    ]

    # Native recurseEvent is generic. Dirty is used to drive every named
    # handler through its public base-method binding, and setters/add/remove
    # below separately exercise actual native lifecycle delivery.
    class Child(w.Widget):
        def on_dirty(self, event: Any) -> None:
            calls.append("dirty")
            saved.append(event)

    root, child = w.Widget(), Child()
    root.add_child(child)
    try:
        for typename, handler in names:
            event = getattr(w, typename)(context=w.EventContext())
            assert isinstance(event, w.BaseEvent)
            getattr(w.Widget, handler)(root, event)
        assert "dirty" in calls
        assert isinstance(w.HoverKeyEvent(), w.KeyBaseEvent)
        assert isinstance(w.HoverKeyEvent(), w.PositionBaseEvent)
        assert isinstance(w.DragHoverEvent(), w.DragBaseEvent)
        assert w.Widget.HoverEvent is w.HoverEvent
        assert w.BaseEvent().get_target() is None
        with pytest.raises(StaleHandleError):
            saved[0].is_consumed()
    finally:
        root.close()


def test_owned_records_path_lifetimes_nullable_targets_and_fields() -> None:
    event = w.PathDropEvent(paths=["/tmp/original", "unicode-λ"], pos=Vec(2, 3))
    assert event.paths == ["/tmp/original", "unicode-λ"]
    copy = event.paths
    copy.append("not-a-native-mutation")
    assert len(event.paths) == 2
    event.paths = ["replacement"]
    assert event.paths == ["replacement"]
    key = w.HoverKeyEvent(key=65, scancode=12, key_name="a", action=1, mods=0)
    assert key.is_key_command(65)
    assert not key.is_key_command(66)
    assert key.key == 65 and key.scancode == 12 and key.action == 1
    text_event = w.HoverTextEvent(codepoint=0x1F642)
    assert isinstance(text_event, w.TextBaseEvent) and text_event.codepoint == 0x1F642
    context = w.EventContext()
    base = w.BaseEvent(context=context)
    base.consume(None)
    assert base.is_consumed() and base.get_target() is None
    assert not base.is_propagating()
    base.unconsume()
    context.propagating = True
    assert not base.is_consumed() and base.is_propagating()
    target = w.Widget()
    try:
        base.set_target(target)
        assert base.get_target() is target and not base.is_consumed()
        base.stop_propagating()
        assert not base.is_propagating() and not base.is_consumed()
        base.set_target(None)
        assert base.get_target() is None
    finally:
        target.close()
    base.context = None
    base.consume(None)
    assert not base.is_consumed() and base.is_propagating()


@pytest.mark.parametrize("event_type", [w.DragHoverEvent, w.DragEnterEvent, w.DragLeaveEvent, w.DragDropEvent])
def test_drag_origin_is_nullable_and_invalidated(event_type: Any) -> None:
    origin = w.Widget()
    event = event_type(origin=origin, button=2)
    assert event.origin is origin and event.button == 2
    event.origin = None
    assert event.origin is None
    event.origin = origin
    origin.close()
    with pytest.raises(StaleHandleError):
        _ = event.origin
    event.origin = None
    assert event.origin is None


def test_callback_exception_is_not_replayed_and_mutation_is_guarded() -> None:
    seen: list[int] = []

    class Child(w.Widget):
        def on_dirty(self, event: Any) -> None:
            super().on_dirty(event)
            raise RuntimeError("intentional-widget-event-error")

    class Grandchild(w.Widget):
        def on_dirty(self, event: Any) -> None:
            seen.append(1)
            with pytest.raises(OwnershipError):
                self.close()
            self.request_delete()

    root, child, grandchild = w.Widget(), Child(), Grandchild()
    child.add_child(grandchild)
    root.add_child(child)
    try:
        root.on_dirty(w.DirtyEvent())
        assert seen == [1]  # no automatic replay after super() already ran
        root.step()
        assert not grandchild.alive
    finally:
        root.close()
