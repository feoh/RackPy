# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's menu helper templates (helpers.hpp), as Python factories (RP-26B).

``createMenuItem<TMenuItem>(...)`` and its siblings are C++ templates that
build a menu item subclass around ``std::function`` callbacks. Here they take
Python callables and an optional class (``item_type``) in place of the
template parameter, and build the same subclass in Python. The callables live
as long as Rack's menu item: Rack deletes the item, and with it the last
reference, when the menu closes. A callback that touches a module deleted
meanwhile raises ``StaleHandleError``, which RackPy contains like any
callback error.

The ``*_ptr_*`` helpers take a :class:`Ref` (or any object with a ``value``
attribute) instead of a C++ pointer; ``None`` behaves like a null pointer.
"""

from collections.abc import Callable, Sequence
from typing import Any, Protocol, overload

from rackpy.raw import ui as _ui

__all__ = [
    "Ref",
    "create_bool_menu_item",
    "create_bool_ptr_menu_item",
    "create_check_menu_item",
    "create_index_ptr_submenu_item",
    "create_index_submenu_item",
    "create_menu_item",
    "create_menu_label",
    "create_submenu_item",
]


class _Value(Protocol):
    value: Any


class Ref:
    """A mutable value for the ``*_ptr_*`` helpers, standing in for a C++ pointer to a field."""

    def __init__(self, value: Any = None) -> None:
        self.value = value

    def __repr__(self) -> str:
        return f"Ref({self.value!r})"


def _item[T: _ui.MenuItem](item_type: type[T] | None) -> type[T]:
    cls = _ui.MenuItem if item_type is None else item_type
    if not (isinstance(cls, type) and issubclass(cls, _ui.MenuItem)):
        raise TypeError("item_type must be a rackpy.raw.ui.MenuItem subclass")
    return cls  # type: ignore[return-value]


@overload
def create_menu_label(text: str, label_type: None = None) -> _ui.MenuLabel: ...
@overload
def create_menu_label[T: _ui.MenuLabel](text: str, label_type: type[T]) -> T: ...
def create_menu_label(text: str, label_type: type[_ui.MenuLabel] | None = None) -> _ui.MenuLabel:
    """Rack's ``createMenuLabel<TMenuLabel>(text)``."""
    cls = _ui.MenuLabel if label_type is None else label_type
    if not (isinstance(cls, type) and issubclass(cls, _ui.MenuLabel)):
        raise TypeError("label_type must be a rackpy.raw.ui.MenuLabel subclass")
    label = cls()
    label.text = text
    return label


def create_menu_item(
    text: str,
    right_text: str = "",
    action: Callable[[], object] | None = None,
    disabled: bool = False,
    always_consume: bool = False,
    *,
    item_type: type[_ui.MenuItem] | None = None,
) -> _ui.MenuItem:
    """Rack's ``createMenuItem<TMenuItem>(text, rightText[, action, disabled, alwaysConsume])``.

    With ``action``, choosing the item calls it; ``always_consume`` keeps the
    event consumed (the menu closes) even if Ctrl is held.
    """
    base = _item(item_type)
    if action is None:
        item = base()
    else:
        if not callable(action):
            raise TypeError("action must be callable")
        act = action

        class ActionItem(base):  # type: ignore[valid-type,misc]
            def on_action(self, event: Any) -> None:
                act()
                if always_consume:
                    event.consume(self)

        item = ActionItem()
        item.disabled = disabled
    item.text, item.right_text = text, right_text
    return item


def create_check_menu_item(
    text: str,
    right_text: str,
    checked: Callable[[], bool],
    action: Callable[[], object],
    disabled: bool = False,
    always_consume: bool = False,
    *,
    item_type: type[_ui.MenuItem] | None = None,
) -> _ui.MenuItem:
    """Rack's ``createCheckMenuItem``: a check mark after ``right_text`` while ``checked()`` is true."""
    base = _item(item_type)
    for f in (checked, action):
        if not callable(f):
            raise TypeError("checked and action must be callable")

    class CheckItem(base):  # type: ignore[valid-type,misc]
        def step(self) -> None:
            self.right_text = right_text
            if checked():
                self.right_text += ("  " if right_text else "") + _ui.CHECKMARK_STRING
            super().step()

        def on_action(self, event: Any) -> None:
            action()
            if always_consume:
                event.consume(self)

    item = CheckItem()
    item.text, item.right_text, item.disabled = text, right_text, disabled
    return item


def create_bool_menu_item(
    text: str,
    right_text: str,
    getter: Callable[[], bool],
    setter: Callable[[bool], object],
    disabled: bool = False,
    always_consume: bool = False,
    *,
    item_type: type[_ui.MenuItem] | None = None,
) -> _ui.MenuItem:
    """Rack's ``createBoolMenuItem``: toggles a bool through ``getter`` and ``setter``."""
    return create_check_menu_item(
        text, right_text, getter, lambda: setter(not getter()), disabled, always_consume, item_type=item_type
    )


def create_bool_ptr_menu_item(text: str, right_text: str, ptr: _Value | None) -> _ui.MenuItem:
    """Rack's ``createBoolPtrMenuItem``: toggles ``ptr.value``; ``None`` reads as false and ignores writes."""

    def get() -> bool:
        return bool(ptr.value) if ptr is not None else False

    def put(value: bool) -> None:
        if ptr is not None:
            ptr.value = value

    return create_bool_menu_item(text, right_text, get, put)


def create_submenu_item(
    text: str,
    right_text: str,
    create_menu: Callable[[_ui.Menu], object],
    disabled: bool = False,
    *,
    item_type: type[_ui.MenuItem] | None = None,
) -> _ui.MenuItem:
    """Rack's ``createSubmenuItem``: when hovered, a submenu that ``create_menu(menu)`` fills."""
    base = _item(item_type)
    if not callable(create_menu):
        raise TypeError("create_menu must be callable")

    class SubmenuItem(base):  # type: ignore[valid-type,misc]
        def create_child_menu(self) -> _ui.Menu:
            menu = _ui.Menu()
            create_menu(menu)
            return menu

    item = SubmenuItem()
    item.text = text
    item.right_text = right_text + ("  " if right_text else "") + _ui.RIGHT_ARROW
    item.disabled = disabled
    return item


def _is(getter: Callable[[], int], index: int) -> Callable[[], bool]:
    return lambda: getter() == index


def _choose(setter: Callable[[int], object], index: int) -> Callable[[], object]:
    return lambda: setter(index)


def create_index_submenu_item(
    text: str,
    labels: Sequence[str],
    getter: Callable[[], int],
    setter: Callable[[int], object],
    disabled: bool = False,
    always_consume: bool = False,
    *,
    item_type: type[_ui.MenuItem] | None = None,
) -> _ui.MenuItem:
    """Rack's ``createIndexSubmenuItem``: a submenu of ``labels`` with a check mark on index ``getter()``."""
    base = _item(item_type)
    names = list(labels)
    for f in (getter, setter):
        if not callable(f):
            raise TypeError("getter and setter must be callable")

    class IndexItem(base):  # type: ignore[valid-type,misc]
        def step(self) -> None:
            index = getter()
            label = names[index] if 0 <= index < len(names) else ""
            self.right_text = label + "  " + _ui.RIGHT_ARROW
            super().step()

        def create_child_menu(self) -> _ui.Menu:
            menu = _ui.Menu()
            for i, name in enumerate(names):
                menu.add_child(
                    create_check_menu_item(name, "", _is(getter, i), _choose(setter, i), False, always_consume)
                )
            return menu

    item = IndexItem()
    item.text, item.disabled = text, disabled
    return item


def create_index_ptr_submenu_item(text: str, labels: Sequence[str], ptr: _Value | None) -> _ui.MenuItem:
    """Rack's ``createIndexPtrSubmenuItem``: chooses ``ptr.value`` among the indices of ``labels``."""

    def get() -> int:
        return int(ptr.value) if ptr is not None else 0

    def put(index: int) -> None:
        if ptr is not None:
            ptr.value = index

    return create_index_submenu_item(text, labels, get, put)
