# SPDX-License-Identifier: GPL-3.0-or-later
"""Value types RackPy shares across the API: colours, quantities and JSON. Available inside Rack only.

``Color`` is Rack's ``NVGcolor``: an immutable value. ``ValueQuantity`` is
RackPy's own Quantity that stores its value natively, so Rack may read it from
any thread. ``JSON`` is what every Rack API that takes or returns ``json_t``
accepts and gives back (the conversion rules are in the values reference).
"""

from rackpy.raw._types import ValueQuantity
from rackpy.raw.nanovg import NVGcolor as Color

__all__ = ["JSON", "Color", "ValueQuantity"]

type JSON = bool | int | float | str | list[JSON] | tuple[JSON, ...] | dict[str, JSON] | None
"""A value that converts to and from Rack's json_t.

No NaN or infinity, str keys, no NUL characters, at most 64 levels deep.
"""
