# SPDX-License-Identifier: GPL-3.0-or-later
"""Python drawing callbacks, driven by Rack's own FramebufferWidget rendering."""

from pathlib import Path
from typing import Any

import pytest

from rackpy.errors import OwnershipError, StaleHandleError
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec


def framed(*children: w.Widget) -> w.FramebufferWidget:
    frame = w.FramebufferWidget()
    frame.box = Rect(Vec(), Vec(40, 40))
    for child in children:
        frame.add_child(child)
    return frame


def test_rack_rendering_calls_python_draw_layer_and_legacy_draw_with_scoped_args() -> None:
    seen: list[Any] = []
    retained: list[Any] = []

    class Leaf(w.Widget):
        def draw(self, args: Any) -> None:
            assert args.vg.alive and args.fb is not None and args.fb.alive
            seen.append(("draw", args.clip_box))
            retained.append(args)

        def draw_vg(self, graphics: Any) -> None:
            # Rack's drawChild calls the deprecated draw(NVGcontext*) after draw(args).
            with pytest.warns(DeprecationWarning, match="draw_vg"):
                super().draw_vg(graphics)
            seen.append(("draw_vg", graphics.alive))
            retained.append(graphics)

        def draw_layer(self, args: Any, layer: int) -> None:
            seen.append(("layer", layer, args.clip_box))

    class Parent(w.Widget):
        def draw(self, args: Any) -> None:
            super().draw(args)  # Rack's Widget::draw reaches the child's overrides
            self.draw_child(leaf, args, 1)  # a layer reaches draw_layer
            with pytest.raises(OwnershipError):
                self.remove_child(leaf)
            with pytest.raises(OwnershipError):
                frame.render()  # no nested explicit render
            seen.append("parent")

    parent, leaf = Parent(), Leaf()
    parent.box = Rect(Vec(4, 4), Vec(30, 30))
    leaf.box = Rect(Vec(2, 3), Vec(10, 10))
    parent.add_child(leaf)
    frame = framed(parent)
    try:
        frame.render()
        # Clip boxes follow Rack's drawChild: intersected with the child, in its coordinates.
        clip = Rect(Vec(), Vec(10, 10))
        assert seen == [("draw", clip), ("draw_vg", True), ("layer", 1, clip), "parent"]
        for ref in retained:
            with pytest.raises(StaleHandleError):
                _ = ref.alive if isinstance(ref, w.GraphicsContext) else ref.clip_box
        with pytest.raises(StaleHandleError):
            parent.draw_child(leaf, retained[0])
        with pytest.raises(TypeError):
            parent.draw_child(None, w.DrawArgs())  # type: ignore[arg-type]
        with pytest.raises(ValueError):
            parent.draw(w.DrawArgs())  # an owned DrawArgs has no live GraphicsContext
    finally:
        frame.close()
    assert not leaf.alive


def test_draw_errors_are_contained_and_siblings_still_draw() -> None:
    drawn: list[int] = []

    class Bad(w.Widget):
        def draw(self, args: Any) -> None:
            raise RuntimeError("intentional-widget-draw-error")

    class Good(w.Widget):
        def draw(self, args: Any) -> None:
            drawn.append(1)

    bad, good = Bad(), Good()
    bad.box = good.box = Rect(Vec(), Vec(5, 5))
    frame = framed(bad, good)
    try:
        frame.render()
        frame.render()
        assert drawn == [1, 1]
        assert bad.alive and good.alive
    finally:
        frame.close()


def test_framebuffer_subclass_draw_framebuffer_and_base_call() -> None:
    calls: list[object] = []

    class Frame(w.FramebufferWidget):
        def draw_framebuffer(self) -> None:
            calls.append(self.get_framebuffer() is not None)
            super().draw_framebuffer()

    class Leaf(w.Widget):
        def draw(self, args: Any) -> None:
            calls.append("leaf")

    frame, leaf = Frame(), Leaf()
    frame.box = leaf.box = Rect(Vec(), Vec(20, 20))
    frame.add_child(leaf)
    try:
        frame.render()
        assert calls == [True, "leaf"]
        frame.delete_framebuffer()
        with pytest.raises(OwnershipError):
            frame.draw_framebuffer()  # the base needs a rendered framebuffer
    finally:
        frame.close()


def test_runnable_widget_snippet() -> None:
    path = Path(__file__).resolve().parents[3] / "docs" / "snippets" / "widget_subclass.py"
    scope: dict[str, Any] = {}
    exec(compile(path.read_text(), str(path), "exec"), scope)
    assert scope["exercise_widgets"]() == (1, 1)
