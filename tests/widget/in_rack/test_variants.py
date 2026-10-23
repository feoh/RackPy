# SPDX-License-Identifier: GPL-3.0-or-later
"""Typed widget variants use real SDK bases and preserve subclass identity."""

import math
from typing import Any

import pytest

from rackpy.errors import StaleHandleError
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec
from rackpy.raw.window import Svg


@pytest.mark.parametrize(
    "name",
    [
        "Widget",
        "TransparentWidget",
        "OpaqueWidget",
        "TransformWidget",
        "ZoomWidget",
        "SvgWidget",
        "FramebufferWidget",
        "OpenGlWidget",
    ],
)
def test_every_variant_subclass_round_trip_and_native_step(name: str) -> None:
    base = getattr(w, name)
    calls: list[int] = []

    def step(self: Any) -> None:
        calls.append(1)
        base.step(self)

    child_type = type("Child", (base,), {"step": step})
    root, child = w.Widget(), child_type()
    try:
        root.add_child(child)
        assert root.children[0] is child and child.parent is root
        root.step()
        assert calls == [1]
        root.remove_child(child)
        assert child.get_parent() is None and child.alive
        root.add_child(child)
    finally:
        root.close()
    assert not child.alive


def test_transparent_opaque_and_zoom_event_base_semantics() -> None:
    seen: list[object] = []

    class Child(w.Widget):
        def on_hover(self, event: Any) -> None:
            seen.append(event.pos)

    for parent_type, expected in [(w.TransparentWidget, 0), (w.OpaqueWidget, 1), (w.ZoomWidget, 1)]:
        parent, child = parent_type(), Child()
        child.box = Rect(Vec(), Vec(100, 100))
        parent.add_child(child)
        event = w.HoverEvent(pos=Vec(20, 30), context=w.EventContext())
        try:
            if isinstance(parent, w.ZoomWidget):
                parent.set_zoom(2)
            before = len(seen)
            parent.on_hover(event)
            assert len(seen) - before == expected
            if isinstance(parent, w.OpaqueWidget):
                assert event.get_target() is parent and event.is_consumed()
            if isinstance(parent, w.ZoomWidget):
                assert seen[-1] == Vec(10, 15)
                # SDK applies ZoomWidget's scale before its ancestor check.
                assert child.get_relative_offset(Vec(3, 4), parent) == Vec(6, 8)
                assert child.get_absolute_zoom() == 2
        finally:
            parent.close()


def test_transform_array_write_through_copy_math_and_invalidation() -> None:
    root = w.TransformWidget()
    matrix = root.transform
    assert tuple(matrix) == (1, 0, 0, 1, 0, 0)
    root.translate(Vec(2, 3))
    assert matrix[4] == 2 and matrix[5] == 3
    root.scale(Vec(2, 4))
    assert matrix[0] == 2 and matrix[3] == 4
    root.identity()
    root.rotate(math.pi / 2, Vec(5, 6))
    assert matrix[0] == pytest.approx(0, abs=1e-6)
    assert matrix[1] == pytest.approx(1)
    matrix[4] = 42
    snapshot = matrix.copy()
    root.transform = (2, 0, 0, 2, 3, 4)
    assert matrix[4] == 3 and snapshot[4] == 42
    with pytest.raises(IndexError):
        _ = matrix[6]
    with pytest.raises((TypeError, ValueError)):
        root.transform = (1, 2)
    root.close()
    with pytest.raises(StaleHandleError):
        matrix.copy()


def test_raw_field_geometry_interoperability_and_eventful_setters() -> None:
    events: list[str] = []

    class Child(w.Widget):
        def on_resize(self, event: Any) -> None:
            events.append("resize")

        def on_show(self, event: Any) -> None:
            events.append("show")

        def on_hide(self, event: Any) -> None:
            events.append("hide")

    root = Child()
    try:
        root.box = Rect(Vec(2, 3), Vec(4, 5))
        assert events == []
        box, pos = root.box, root.box.pos
        assert box.get_center() == Vec(4, 5.5)
        assert pos.norm() == pytest.approx(math.sqrt(13))
        assert Vec(1, 1).plus(pos) == Vec(3, 4)
        assert Rect(pos, root.box.size) == box
        assert Vec(pos) == Vec(2, 3) and Rect(box) == root.get_box()
        pos *= 2
        assert pos == Vec(4, 6) and root.get_position() == pos
        root.set_size(root.box.pos)
        assert events == ["resize"]
        root.visible = False
        assert events == ["resize"]
        root.show()
        root.hide()
        assert events == ["resize", "show", "hide"]
    finally:
        root.close()
    with pytest.raises(StaleHandleError):
        pos.norm()


def test_svg_and_framebuffer_fields_lifetimes_and_native_render() -> None:
    # Original test artwork, not Rack or Fundamental panel graphics.
    svg = Svg()
    svg.load_string(
        '<svg width="17" height="23" xmlns="http://www.w3.org/2000/svg">'
        '<rect width="17" height="23" fill="#124578"/></svg>'
    )
    node = w.SvgWidget()
    frame = w.FramebufferWidget()
    try:
        node.set_svg(svg)
        assert node.svg is svg and node.get_size() == Vec(17, 23)
        node.svg = None
        assert node.get_size() == Vec(17, 23)  # raw field does not wrap
        node.wrap()
        assert node.get_size() == Vec()
        node.set_svg(svg)
        frame.add_child(node)
        margin = frame.viewport_margin
        margin.x = 20
        margin.y = 30
        assert frame.viewport_margin == Vec(20, 30)
        frame.set_dirty(False)
        assert not bool(frame.dirty)
        frame.on_dirty(w.DirtyEvent())
        assert bool(frame.dirty)
        assert frame.get_image_handle() == -1 and frame.get_framebuffer() is None
        frame.render()
        assert frame.get_image_handle() >= 0
        assert frame.get_framebuffer_size() == Vec(17, 23)
        resource = frame.get_framebuffer()
        assert resource is not None and resource.alive
        frame.delete_framebuffer()
        assert frame.get_framebuffer() is None
        with pytest.raises(StaleHandleError):
            _ = resource.alive
        frame.oversample = float("inf")
        with pytest.raises(ValueError):
            frame.render()
    finally:
        frame.close()
    assert not node.alive
    assert svg.get_size() == Vec(17, 23)  # shared CPU asset outlives widget
    with pytest.raises(StaleHandleError):
        _ = margin.x
