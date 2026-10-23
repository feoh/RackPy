# SPDX-License-Identifier: GPL-3.0-or-later
"""Python widgets under Rack-owned parents: the scene, module widgets and browser previews."""

from typing import Any

import _rackpy_host
import pytest

import rackpy._bootstrap
from rackpy.errors import OwnershipError, StaleHandleError
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec


def test_scene_attachment_transfers_to_rack_and_back() -> None:
    node = w.Widget()
    node.attach_to_scene()
    try:
        scene = node.parent
        assert isinstance(scene, w.HostWidget) and scene.alive and scene.parent is None
        assert node.get_parent() is scene and node.is_descendant_of(scene)
        with pytest.raises(OwnershipError):
            node.close()  # Rack's scene owns it now
        with pytest.raises(OwnershipError):
            node.attach_to_scene()
    finally:
        node.remove_from_parent()
    assert node.parent is None and node.alive
    node.remove_from_parent()  # already detached: nothing to do
    node.close()
    assert not node.alive


def test_scene_widget_receives_rack_window_teardown() -> None:
    # Left in the scene on purpose: Rack's Window destructor sends ContextDestroy
    # through the scene before RackPy's context shutdown sweeps managed widgets.
    # A simulated recreation (below) adds destroy/create pairs; the real teardown is a last, unpaired destroy.
    events: list[str] = []

    class Probe(w.Widget):
        def on_context_destroy(self, event: Any) -> None:
            assert event.vg is not None and event.vg.alive
            events.append("destroy")
            super().on_context_destroy(event)

        def on_context_create(self, event: Any) -> None:
            events.append("create")
            super().on_context_create(event)

    node = Probe()
    node.attach_to_scene()
    original_stop = rackpy._bootstrap.stop

    def stop() -> None:
        original_stop()
        assert events[-1] == "destroy" and events.count("destroy") == events.count("create") + 1
        assert not node.alive
        _rackpy_host.log_info("scene-widget-teardown-ok")

    rackpy._bootstrap.stop = stop


def test_module_removal_deletes_python_children_like_rack() -> None:
    events: list[object] = []

    class Badge(w.Widget):
        def on_remove(self, event: Any) -> None:
            events.append(isinstance(self.parent, w.HostWidget) and self.parent.alive)
            super().on_remove(event)

    from _rackpy_host import _testing as hooks  # the live-UI run uses the test-hooks bundle

    probe = hooks.add_probe_module()
    badge = Badge()
    hooks.module_widget_add_child(probe, badge)
    module_widget = badge.parent
    assert isinstance(module_widget, w.HostWidget) and bool(module_widget.alive)
    with pytest.raises(OwnershipError):
        badge.close()  # the module widget owns it
    hooks.remove_probe_module(probe)  # as Rack's "Delete module": GIL released, engine locked
    assert events == [True]  # one Remove event, while its parent still exists
    assert not badge.alive and not module_widget.alive and not probe.alive
    with pytest.raises(StaleHandleError):
        badge.get_box()


def test_python_widget_in_a_null_module_browser_preview() -> None:
    calls: list[str] = []

    class Badge(w.Widget):
        def step(self) -> None:
            calls.append("step")
            super().step()

        def draw(self, args: Any) -> None:
            calls.append("draw")

        def on_remove(self, event: Any) -> None:
            calls.append("remove")

    from _rackpy_host import _testing as hooks

    badge = Badge()
    badge.box = Rect(Vec(2, 2), Vec(10, 10))
    hooks.render_browser_preview(badge)  # built, stepped, rendered and deleted as the browser does
    assert calls == ["step", "draw", "remove"]
    assert not badge.alive


def test_simulated_graphics_context_recreation() -> None:
    seen: list[str] = []

    class Frame(w.FramebufferWidget):
        def on_context_destroy(self, event: Any) -> None:
            seen.append("destroy")  # no super(): RackPy still frees the framebuffer

        def on_context_create(self, event: Any) -> None:
            seen.append("create")
            super().on_context_create(event)

    from _rackpy_host import _testing as hooks

    in_scene, detached = Frame(), w.FramebufferWidget()
    for frame in (in_scene, detached):
        frame.box = Rect(Vec(), Vec(10, 10))
        leaf = w.Widget()
        leaf.box = frame.box.copy()
        frame.add_child(leaf)
    in_scene.attach_to_scene()
    try:
        held = []
        for frame in (in_scene, detached):
            frame.render()
            frame.set_dirty(False)
            framebuffer = frame.get_framebuffer()
            assert framebuffer is not None
            held.append(framebuffer)
        hooks.recreate_graphics_context()
        assert seen == ["destroy", "create"]
        for frame, framebuffer in zip((in_scene, detached), held, strict=True):
            assert frame.get_framebuffer() is None and frame.get_image_handle() == -1
            assert bool(frame.dirty)
            with pytest.raises(StaleHandleError):
                _ = framebuffer.alive
            frame.render()  # the new context renders again
            assert frame.get_framebuffer() is not None
    finally:
        in_scene.remove_from_parent()
        in_scene.close()
        detached.close()
