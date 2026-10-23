# SPDX-License-Identifier: GPL-3.0-or-later
"""Widget event records backed by owned values or expiring native callback views.

The Python classes preserve the SDK prototype hierarchy. The native reference
performs every lifetime, constness and UI-thread check; no pointer is exposed.
"""

from collections.abc import Sequence
from types import ModuleType
from typing import Any


class _Binding:
    native: Any = None  # rackpy.raw.widget, set once by register()


def _field(name: str) -> property:
    def get(self: _Record) -> Any:
        return self._ref.get(name)

    def set_(self: _Record, value: object) -> None:
        self._ref.set(name, value)

    return property(get, set_)


class _Record:
    __slots__ = ("_ref",)
    _kind = ""
    _ref: Any

    def __init__(self, **fields: object) -> None:
        ref = fields.pop("_ref", None)
        self._ref = ref if ref is not None else _Binding.native._make_event(self._kind)
        for name, value in fields.items():
            setattr(self, name, value)


class EventContext(_Record):
    __slots__ = ()
    propagating = _field("propagating")
    consumed = _field("consumed")
    target = _field("target")

    def __init__(self, **fields: object) -> None:
        ref = fields.pop("_ref", None)
        self._ref = ref if ref is not None else _Binding.native._make_event_context()
        for name, value in fields.items():
            setattr(self, name, value)


class BaseEvent(_Record):
    __slots__ = ()
    _kind = "BaseEvent"
    context = _field("context")

    def stop_propagating(self) -> None:
        self._ref.stop_propagating()

    def is_propagating(self) -> bool:
        return bool(self._ref.is_propagating())

    def set_target(self, widget: object) -> None:
        self._ref.set_target(widget)

    def get_target(self) -> Any:
        return self._ref.get_target()

    def consume(self, widget: object) -> None:
        self._ref.consume(widget)

    def unconsume(self) -> None:
        self._ref.unconsume()

    def is_consumed(self) -> bool:
        return bool(self._ref.is_consumed())


class PositionBaseEvent(_Record):
    __slots__ = ()
    _kind = "PositionBaseEvent"
    pos = _field("pos")


class KeyBaseEvent(_Record):
    __slots__ = ()
    _kind = "KeyBaseEvent"
    key = _field("key")
    scancode = _field("scancode")
    key_name = _field("key_name")
    action = _field("action")
    mods = _field("mods")

    def is_key_command(self, key: int, mods: int = 0) -> bool:
        return bool(self._ref.is_key_command(key, mods))


class TextBaseEvent(_Record):
    __slots__ = ()
    _kind = "TextBaseEvent"
    codepoint = _field("codepoint")


class DragBaseEvent(BaseEvent):
    __slots__ = ()
    _kind = "DragBaseEvent"
    button = _field("button")


class DrawArgs(_Record):
    __slots__ = ()
    _kind = "DrawArgs"
    vg = _field("vg")
    fb = _field("fb")
    clip_box = _field("clip_box")


class HoverEvent(BaseEvent, PositionBaseEvent):
    __slots__ = ()
    _kind = "HoverEvent"
    mouse_delta = _field("mouse_delta")


class ButtonEvent(BaseEvent, PositionBaseEvent):
    __slots__ = ()
    _kind = "ButtonEvent"
    button = _field("button")
    action = _field("action")
    mods = _field("mods")


class DoubleClickEvent(BaseEvent):
    __slots__ = ()
    _kind = "DoubleClickEvent"


class HoverKeyEvent(BaseEvent, PositionBaseEvent, KeyBaseEvent):
    __slots__ = ()
    _kind = "HoverKeyEvent"


class HoverTextEvent(BaseEvent, PositionBaseEvent, TextBaseEvent):
    __slots__ = ()
    _kind = "HoverTextEvent"


class HoverScrollEvent(BaseEvent, PositionBaseEvent):
    __slots__ = ()
    _kind = "HoverScrollEvent"
    scroll_delta = _field("scroll_delta")


class EnterEvent(BaseEvent):
    __slots__ = ()
    _kind = "EnterEvent"


class LeaveEvent(BaseEvent):
    __slots__ = ()
    _kind = "LeaveEvent"


class SelectEvent(BaseEvent):
    __slots__ = ()
    _kind = "SelectEvent"


class DeselectEvent(BaseEvent):
    __slots__ = ()
    _kind = "DeselectEvent"


class SelectKeyEvent(BaseEvent, KeyBaseEvent):
    __slots__ = ()
    _kind = "SelectKeyEvent"


class SelectTextEvent(BaseEvent, TextBaseEvent):
    __slots__ = ()
    _kind = "SelectTextEvent"


class DragStartEvent(DragBaseEvent):
    __slots__ = ()
    _kind = "DragStartEvent"


class DragEndEvent(DragBaseEvent):
    __slots__ = ()
    _kind = "DragEndEvent"


class DragMoveEvent(DragBaseEvent):
    __slots__ = ()
    _kind = "DragMoveEvent"
    mouse_delta = _field("mouse_delta")


class DragHoverEvent(DragBaseEvent, PositionBaseEvent):
    __slots__ = ()
    _kind = "DragHoverEvent"
    origin = _field("origin")
    mouse_delta = _field("mouse_delta")


class DragEnterEvent(DragBaseEvent):
    __slots__ = ()
    _kind = "DragEnterEvent"
    origin = _field("origin")


class DragLeaveEvent(DragBaseEvent):
    __slots__ = ()
    _kind = "DragLeaveEvent"
    origin = _field("origin")


class DragDropEvent(DragBaseEvent):
    __slots__ = ()
    _kind = "DragDropEvent"
    origin = _field("origin")


class PathDropEvent(BaseEvent, PositionBaseEvent):
    __slots__ = ()
    _kind = "PathDropEvent"
    paths = _field("paths")

    def __init__(self, paths: Sequence[str] = (), **fields: object) -> None:
        if "_ref" in fields:
            super().__init__(**fields)
        else:
            super().__init__(paths=paths, **fields)


class ActionEvent(BaseEvent):
    __slots__ = ()
    _kind = "ActionEvent"


class ChangeEvent(BaseEvent):
    __slots__ = ()
    _kind = "ChangeEvent"


class DirtyEvent(BaseEvent):
    __slots__ = ()
    _kind = "DirtyEvent"


class RepositionEvent(BaseEvent):
    __slots__ = ()
    _kind = "RepositionEvent"


class ResizeEvent(BaseEvent):
    __slots__ = ()
    _kind = "ResizeEvent"


class AddEvent(BaseEvent):
    __slots__ = ()
    _kind = "AddEvent"


class RemoveEvent(BaseEvent):
    __slots__ = ()
    _kind = "RemoveEvent"


class ShowEvent(BaseEvent):
    __slots__ = ()
    _kind = "ShowEvent"


class HideEvent(BaseEvent):
    __slots__ = ()
    _kind = "HideEvent"


class ContextCreateEvent(BaseEvent):
    __slots__ = ()
    _kind = "ContextCreateEvent"
    vg = _field("vg")


class ContextDestroyEvent(BaseEvent):
    __slots__ = ()
    _kind = "ContextDestroyEvent"
    vg = _field("vg")


def register(native: ModuleType) -> None:
    _Binding.native = native
    classes = {
        name: cls
        for name, cls in globals().items()
        if isinstance(cls, type) and issubclass(cls, _Record) and not name.startswith("_")
    }
    native._register_event_types(classes)

    def one_widget_base(cls: type, /, **kwargs: object) -> None:
        if sum(issubclass(base, native.Widget) for base in cls.__bases__) > 1:
            raise TypeError(
                "use one native Widget base plus non-Widget Python mixins; multiple native owners are ambiguous"
            )

    native.Widget.__init_subclass__ = classmethod(one_widget_base)
    for name, cls in classes.items():
        cls.__module__ = "rackpy.raw.widget"
        setattr(native, name, cls)
        if name != "EventContext":
            setattr(native.Widget, name, cls)
