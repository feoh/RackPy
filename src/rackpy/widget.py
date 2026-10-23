# SPDX-License-Identifier: GPL-3.0-or-later
"""Detached widget-tree groundwork, usable only in Rack's live UI context.

WidgetNode is a native-only checked adapter, not a subclassable Rack Widget.
Scene attachment, events, drawing and transformed widgets remain planned.
"""

from rackpy.raw.widget import WidgetNode

__all__ = ["WidgetNode"]
