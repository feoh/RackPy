# SPDX-License-Identifier: GPL-3.0-or-later
"""Python widgets in Rack's own scene, including the real window teardown."""

from typing import Any

import _rackpy_host
import pytest

import rackpy._bootstrap
from rackpy.errors import OwnershipError
from rackpy.raw import widget as w


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
    events: list[object] = []

    class Probe(w.Widget):
        def on_context_destroy(self, event: Any) -> None:
            events.append(event.vg is not None and event.vg.alive)
            super().on_context_destroy(event)

    node = Probe()
    node.attach_to_scene()
    original_stop = rackpy._bootstrap.stop

    def stop() -> None:
        original_stop()
        assert events == [True] and not node.alive
        _rackpy_host.log_info("scene-widget-teardown-ok")

    rackpy._bootstrap.stop = stop
