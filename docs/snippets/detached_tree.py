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
        sibling = WidgetNode.create()
        root.add_child(sibling)
        above, below = WidgetNode.create(), WidgetNode.create()
        root.add_child_above(above, child)
        root.add_child_below(below, sibling)
        children = root.children
        assert children.index(child) + 1 == children.index(above)
        assert children.index(below) + 1 == children.index(sibling)
        assert child.parent is root
        assert root.get_children_bounding_box().size.x >= child.get_box().size.x
        root.set_position(Vec(10, 20))
        assert child.get_absolute_offset(Vec()) == Vec(14, 26)
        child.set_size(Vec(20, 12))
        assert child.get_size() == Vec(20, 12)
        position = child.box.pos  # checked raw field alias, no layout events
        position.x = 8
        assert child.get_position() == Vec(8, 6)
        independent = child.box.copy()
        independent.pos.x = 999
        assert position.x == 8
        child.hide()
        assert not child.is_visible()
        child.visible = True  # raw field write; unlike show(), emits no event
        assert child.is_visible()
        child.show()
        assert child.visible

        root.remove_child(child)  # child owns an independent native root again
        assert child.parent is None
        child.close()
        assert not child.alive
        above.request_delete()  # stays live until its parent's native step
        root.step()
        assert not above.alive
    finally:
        root.close()
