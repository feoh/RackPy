# SPDX-License-Identifier: GPL-3.0-or-later
# ruff: noqa: F822  # the names are provided lazily by __getattr__, which warns
"""``rack::event``: Rack's deprecated aliases for the widget event types.

``rack::event::Action`` is ``rack::widget::Widget::ActionEvent`` and so on,
kept by Rack for plugins written before Rack 2. Here each name returns the
same class as :mod:`rackpy.raw.widget` and emits ``DeprecationWarning``.
"""

import warnings
from typing import Any

from rackpy.raw import widget as _widget

# rack::event name -> rackpy.raw.widget class (Rack's widget/Widget.hpp aliases)
_ALIASES = {
    "Base": "BaseEvent",
    "PositionBase": "PositionBaseEvent",
    "KeyBase": "KeyBaseEvent",
    "TextBase": "TextBaseEvent",
    "Hover": "HoverEvent",
    "Button": "ButtonEvent",
    "DoubleClick": "DoubleClickEvent",
    "HoverKey": "HoverKeyEvent",
    "HoverText": "HoverTextEvent",
    "HoverScroll": "HoverScrollEvent",
    "Enter": "EnterEvent",
    "Leave": "LeaveEvent",
    "Select": "SelectEvent",
    "Deselect": "DeselectEvent",
    "SelectKey": "SelectKeyEvent",
    "SelectText": "SelectTextEvent",
    "DragBase": "DragBaseEvent",
    "DragStart": "DragStartEvent",
    "DragEnd": "DragEndEvent",
    "DragMove": "DragMoveEvent",
    "DragHover": "DragHoverEvent",
    "DragEnter": "DragEnterEvent",
    "DragLeave": "DragLeaveEvent",
    "DragDrop": "DragDropEvent",
    "PathDrop": "PathDropEvent",
    "Action": "ActionEvent",
    "Change": "ChangeEvent",
    "Dirty": "DirtyEvent",
    "Reposition": "RepositionEvent",
    "Resize": "ResizeEvent",
    "Add": "AddEvent",
    "Remove": "RemoveEvent",
    "Show": "ShowEvent",
    "Hide": "HideEvent",
}

__all__ = [
    "Action",
    "Add",
    "Base",
    "Button",
    "Change",
    "Deselect",
    "Dirty",
    "DoubleClick",
    "DragBase",
    "DragDrop",
    "DragEnd",
    "DragEnter",
    "DragHover",
    "DragLeave",
    "DragMove",
    "DragStart",
    "Enter",
    "Hide",
    "Hover",
    "HoverKey",
    "HoverScroll",
    "HoverText",
    "KeyBase",
    "Leave",
    "PathDrop",
    "PositionBase",
    "Remove",
    "Reposition",
    "Resize",
    "Select",
    "SelectKey",
    "SelectText",
    "Show",
    "TextBase",
]


def __getattr__(name: str) -> Any:
    if name not in _ALIASES:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    target = _ALIASES[name]
    warnings.warn(
        f"rackpy.raw.event.{name} is Rack's deprecated rack::event::{name}; use rackpy.raw.widget.{target}",
        DeprecationWarning,
        stacklevel=2,
    )
    return getattr(_widget, target)


def __dir__() -> list[str]:
    return __all__
