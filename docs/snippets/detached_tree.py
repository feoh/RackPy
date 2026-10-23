# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.raw.math import Rect, Vec
from rackpy.widget import WidgetNode


def exercise_tree() -> None:
    """Call from Rack's UI thread after its context exists, not plugin startup."""
    root = WidgetNode.create()
    try:
        child = WidgetNode.create()
        child.set_box(Rect(4, 6, 20, 12))
        root.add_child(child)
        assert child.parent is root
        assert root.get_children_bounding_box() == child.get_box()
        root.set_position(Vec(10, 20))
        assert child.get_absolute_offset(Vec()) == Vec(14, 26)
        child.set_size(Vec(20, 12))
        assert child.get_size() == Vec(20, 12)
        child.hide()
        assert not child.is_visible()
        child.show()
        assert child.is_visible()

        root.remove_child(child)  # child owns an independent native root again
        assert child.parent is None
        child.close()
        assert not child.alive
    finally:
        root.close()
