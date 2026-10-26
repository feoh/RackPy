# SPDX-License-Identifier: GPL-3.0-or-later
"""Detached widget-tree groundwork, usable only in Rack's live UI context.

WidgetNode is a native-only checked adapter, not a subclassable Rack Widget.
Subclassable widgets, events, drawing and the typed bases are in
:mod:`rackpy.raw.widget` (``Widget`` and its variants); :mod:`rackpy.drawing` draws in their
callbacks.
"""

from rackpy.raw.widget import WidgetNode

__all__ = ["WidgetNode"]
