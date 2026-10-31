# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38 (an RP-22F follow-up): a widget being deallocated never answers Rack from Python.

While Python deallocates an object (refcount 0, clearing its __dict__), the C++ widget
still exists and Rack may still call its virtuals. A bound method would resurrect the
dying object, so RackPy's resolver lets the native implementation answer, and finding
the Python object of a native pointer skips it. The laboratory's MIDI tests cover the
same rule for ports, drivers and devices; this covers widgets.
"""

import gc
import weakref
from typing import Any

from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec


class Token:
    pass


def test_a_widget_being_deallocated_never_answers_from_python() -> None:
    heard: list[str] = []
    seen: list[tuple[float, float, str]] = []

    class Dying(w.Widget):
        token: object

        def get_relative_offset(self, value: Any, ancestor: Any) -> Vec:
            heard.append("offset")
            return Vec(-1000, -1000)

        def get_relative_zoom(self, ancestor: Any) -> float:
            heard.append("zoom")
            return 7.0

    root = Dying()
    root.box = Rect(Vec(100, 0), Vec(10, 10))
    child = w.Widget()
    child.box = Rect(Vec(5, 0), Vec(1, 1))
    root.add_child(child)
    assert child.get_relative_zoom(None) == 7.0 and heard == ["zoom"]  # alive: Rack asks the parent's override
    heard.clear()

    def during(_: object) -> None:
        # Rack walks up to the dying parent and calls its virtuals; RackPy finds no live Python object there.
        parent = child.parent
        seen.append(
            (child.get_relative_offset(Vec(1, 0), None).x, child.get_relative_zoom(None), type(parent).__name__)
        )

    token = Token()
    root.token = token
    watch = weakref.ref(token, during)
    gone = weakref.ref(root)
    del token, root
    gc.collect()
    assert gone() is None and watch() is None
    assert heard == []  # never resurrected to answer
    assert seen and seen[0][:2] == (106.0, 1.0), seen  # Rack's own answers, through the parent's box
    assert seen[0][2] != "Dying", seen
    assert not child.alive  # deleted with its parent
