# SPDX-License-Identifier: GPL-3.0-or-later
"""Widget's member templates and the key-name functions, in actual Rack."""

from typing import Any

import pytest

from rackpy.errors import OwnershipError
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec

# GLFW's values; RACK_MOD_* are not bound yet.
GLFW_MOD_SHIFT, GLFW_MOD_CONTROL = 1, 2


def check_key_names() -> None:
    assert w.get_key_name(65) == "A"  # GLFW_KEY_A is ASCII
    assert w.get_key_name(10) == ""  # control codes have no name
    assert w.get_key_command_name(65, 0) == "A"
    shift = w.get_key_command_name(65, GLFW_MOD_SHIFT)
    both = w.get_key_command_name(65, GLFW_MOD_SHIFT | GLFW_MOD_CONTROL)
    assert shift.endswith("+A") and len(shift) > 2
    assert both.endswith(shift) and both.count("+") == 2  # Ctrl comes before Shift


def test_startup_only_key_names_need_no_window() -> None:
    check_key_names()


def test_key_names_in_the_live_ui() -> None:
    check_key_names()


def test_typed_ancestor_and_descendant_lookups() -> None:
    class Panel(w.OpaqueWidget):
        pass

    class Knob(w.Widget):
        pass

    root = w.TransparentWidget()
    panel, inner, knob, second = Panel(), w.ZoomWidget(), Knob(), Knob()
    root.add_child(panel)
    panel.add_child(inner)
    inner.add_child(knob)
    root.add_child(second)
    try:
        assert knob.get_ancestor_of_type(Panel) is panel  # a Python subclass
        assert knob.get_ancestor_of_type(w.OpaqueWidget) is panel  # its native base
        assert knob.get_ancestor_of_type(w.TransparentWidget) is root
        assert knob.get_ancestor_of_type(w.Widget) is inner  # the nearest one
        assert knob.get_ancestor_of_type(w.SvgWidget) is None
        assert root.get_ancestor_of_type(w.Widget) is None
        assert root.get_first_descendant_of_type(Knob) is knob  # depth first, in child order
        assert root.get_first_descendant_of_type(w.ZoomWidget) is inner
        assert root.get_first_descendant_of_type(w.FramebufferWidget) is None
        for bad in (int, w.HostWidget, "Widget"):
            with pytest.raises(TypeError):
                root.get_first_descendant_of_type(bad)  # type: ignore[arg-type, type-var]
    finally:
        root.close()


def test_lookups_reach_rack_owned_widgets() -> None:
    node = w.Widget()
    node.attach_to_scene()
    try:
        scene = node.get_ancestor_of_type(w.Widget)  # Rack's Scene is a Widget
        assert isinstance(scene, w.HostWidget) and scene is node.parent
        assert node.get_ancestor_of_type(w.OpaqueWidget) is not None  # so is Rack's Scene
        assert node.get_ancestor_of_type(type("Python", (w.Widget,), {})) is None
    finally:
        node.remove_from_parent()
        node.close()


def test_recurse_event_and_recurse_position_event_follow_rack() -> None:
    seen: list[Any] = []

    class Leaf(w.Widget):
        def __init__(self, name: str, consume: bool = False) -> None:
            super().__init__()
            self.name = name
            self.consume = consume

        def on_dirty(self, event: Any) -> None:
            seen.append(self.name)

        def on_hover(self, event: Any) -> None:
            seen.append((self.name, event.pos))
            if self.consume:
                event.consume(self)

    class Container(w.Widget):
        def on_hover(self, event: Any) -> None:
            # A container that offers hover to its children itself, as Rack's base does.
            self.recurse_position_event(w.Widget.on_hover, event)

        def on_dirty(self, event: Any) -> None:
            with pytest.raises(OwnershipError):
                self.clear_children()  # Rack is iterating these children
            seen.append("guarded")

    root = Container()
    root.box = Rect(Vec(0, 0), Vec(30, 30))
    a, b, hidden = Leaf("a"), Leaf("b", consume=True), Leaf("hidden")
    a.box = Rect(Vec(0, 0), Vec(10, 10))
    b.box = Rect(Vec(5, 5), Vec(10, 10))
    hidden.box = Rect(Vec(0, 0), Vec(20, 20))
    hidden.visible = False
    for child in (a, b, hidden):
        root.add_child(child)
    outer = w.Widget()
    outer.add_child(root)
    try:
        root.recurse_event(w.Widget.on_dirty, w.DirtyEvent(context=w.EventContext()))
        assert seen == ["hidden", "b", "a"]  # topmost first; visibility is not filtered
        seen.clear()
        event = w.HoverEvent(pos=Vec(7, 8), context=w.EventContext())
        outer.on_hover(event)  # Rack's base recursion reaches the Python container
        assert seen == [("b", Vec(2, 3))]  # hidden skipped; b consumes, so a is not offered it
        assert event.get_target() is b
        seen.clear()
        root.recurse_position_event(w.Widget.on_hover, w.HoverEvent(pos=Vec(1, 1), context=w.EventContext()))
        assert seen == [("a", Vec(1, 1))]  # b's box does not contain (1, 1)
        seen.clear()
        outer.recurse_event(w.Widget.on_dirty, w.DirtyEvent())
        assert seen == ["guarded"]
        with pytest.raises(TypeError):
            root.recurse_event(w.Widget.on_button, w.DirtyEvent())  # the event does not match the handler
        with pytest.raises(TypeError):
            root.recurse_position_event(w.Widget.on_dirty, w.DirtyEvent())  # no position
        with pytest.raises(TypeError):
            root.recurse_event(print, w.DirtyEvent())
    finally:
        outer.close()
