# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-42: the managed raw Widget's own values and the event fields, read back (widgets-11 review gaps)."""

from typing import Any

from rackpy import raw
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec


def test_raw_widget_tree_geometry_and_visibility_queries() -> None:
    events: list[str] = []

    class Seen(w.Widget):
        def on_show(self, event: Any) -> None:
            events.append("show")
            super().on_show(event)

        def on_hide(self, event: Any) -> None:
            events.append("hide")
            super().on_hide(event)

    root, a, b = w.Widget(), w.Widget(), Seen()
    above, below, stranger = w.Widget(), w.Widget(), w.Widget()
    try:
        assert not root.requested_delete and root.get_parent() is None
        root.box = Rect(Vec(10, 20), Vec(200, 100))
        a.box = Rect(Vec(5, 6), Vec(20, 30))
        b.box = Rect(Vec(50, 40), Vec(10, 10))
        above.box = below.box = Rect(Vec(0, 0), Vec(1, 1))  # a new Widget's box is infinitely big (Rack's default)
        root.add_child(a)
        root.add_child(b)
        root.add_child_above(above, a)  # directly after a
        root.add_child_below(below, b)  # directly before b
        assert list(root.children) == [a, above, below, b]
        assert root.has_child(a) and root.has_child(below) and not root.has_child(stranger) and not root.has_child(None)
        a.set_position(Vec(7, 8))
        assert a.box.pos == Vec(7, 8) and a.get_position() == Vec(7, 8)
        # Rack's bounding boxes: the extremes of the children's boxes, the visible ones only for the second.
        assert root.get_children_bounding_box() == Rect(Vec(0, 0), Vec(60, 50))
        assert root.get_visible_children_bounding_box() == Rect(Vec(0, 0), Vec(60, 50))
        assert b.is_visible()
        b.set_visible(False)
        b.set_visible(False)  # no change: no second event
        assert not b.is_visible() and events == ["hide"]
        assert root.get_children_bounding_box() == Rect(Vec(0, 0), Vec(60, 50))
        assert root.get_visible_children_bounding_box() == Rect(Vec(0, 0), Vec(27, 38))
        b.set_visible(True)
        assert b.is_visible() and events == ["hide", "show"]
        # A widget's offset and zoom are the sums and the product up its parents.
        assert a.get_absolute_offset(Vec(1, 1)) == Vec(18, 29)
        assert a.get_relative_offset(Vec(1, 1), root) == Vec(8, 9)
        assert a.get_relative_zoom(root) == 1.0 and a.get_absolute_zoom() == 1.0
        # getViewport: the rectangle clamped to what the widget shows of its parent's (and so its own) box.
        assert root.get_viewport(Rect(Vec(0, 0), Vec(500, 500))) == Rect(Vec(0, 0), Vec(200, 100))
        assert a.get_viewport(Rect(Vec(0, 0), Vec(100, 100))) == Rect(Vec(0, 0), Vec(20, 30))
        root.request_delete()
        assert root.requested_delete
    finally:
        for widget in (root, stranger):
            widget.close()


def test_event_fields_are_read_back_as_set() -> None:
    context = w.EventContext()
    press = w.ButtonEvent(button=1, action=raw.glfw.GLFW_PRESS, mods=raw.macros.RACK_MOD_CTRL, context=context)
    assert (press.button, press.action, press.mods) == (1, raw.glfw.GLFW_PRESS, raw.macros.RACK_MOD_CTRL)
    assert w.HoverEvent(mouse_delta=Vec(2, 3)).mouse_delta == Vec(2, 3)
    assert w.DragHoverEvent(mouse_delta=Vec(-4, 5)).mouse_delta == Vec(-4, 5)
    assert w.DragMoveEvent(mouse_delta=Vec(6, -7)).mouse_delta == Vec(6, -7)
    assert isinstance(w.HoverEvent(), w.Widget.BaseEvent)  # the nested alias is the same class
    assert w.Widget.BaseEvent is w.BaseEvent
    # isKeyCommand uses the key's name to find the key on other layouts, and the modifiers must match exactly.
    layout = w.HoverKeyEvent(key=raw.glfw.GLFW_KEY_A, key_name="q", action=raw.glfw.GLFW_PRESS)
    assert layout.key_name == "q"
    assert layout.is_key_command(ord("Q")) and not layout.is_key_command(raw.glfw.GLFW_KEY_A)
    ctrl = w.HoverKeyEvent(key=raw.glfw.GLFW_KEY_A, key_name="a", mods=raw.macros.RACK_MOD_CTRL)
    assert ctrl.mods == raw.macros.RACK_MOD_CTRL
    assert ctrl.is_key_command(ord("A"), raw.macros.RACK_MOD_CTRL) and not ctrl.is_key_command(ord("A"))
