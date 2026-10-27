# SPDX-License-Identifier: GPL-3.0-or-later
# Stub for rackpy.raw.ui (native/bindings/ui/ui.cpp), RP-26A.
from enum import IntEnum
from typing import ClassVar

from rackpy.raw import Quantity
from rackpy.raw.math import Rect, Vec
from rackpy.raw.nanovg import NVGcolor
from rackpy.raw.widget import HostWidget, OpaqueWidget, TransparentWidget, Widget

CHECKMARK_STRING: str
RIGHT_ARROW: str

def CHECKMARK(cond: bool) -> str: ...
def set_theme(bg: NVGcolor, fg: NVGcolor) -> None: ...
def refresh_theme() -> None: ...

class Label(Widget):
    class Alignment(IntEnum):
        LEFT_ALIGNMENT = 0
        CENTER_ALIGNMENT = 1
        RIGHT_ALIGNMENT = 2

    LEFT_ALIGNMENT: ClassVar[Alignment]
    CENTER_ALIGNMENT: ClassVar[Alignment]
    RIGHT_ALIGNMENT: ClassVar[Alignment]
    text: str
    font_size: float
    line_height: float
    color: NVGcolor
    alignment: Alignment

class Button(OpaqueWidget):
    text: str
    quantity: Quantity | None

class ChoiceButton(Button): ...
class RadioButton(Button): ...
class OptionButton(RadioButton): ...

class Slider(OpaqueWidget):
    quantity: Quantity | None

class ProgressBar(Widget):
    quantity: Quantity | None

class Tooltip(Widget):
    text: str

class TooltipOverlay(TransparentWidget): ...
class List(OpaqueWidget): ...

class SequentialLayout(Widget):
    class Orientation(IntEnum):
        HORIZONTAL_ORIENTATION = 0
        VERTICAL_ORIENTATION = 1

    class Alignment(IntEnum):
        LEFT_ALIGNMENT = 0
        CENTER_ALIGNMENT = 1
        RIGHT_ALIGNMENT = 2
        TOP_ALIGNMENT = 0
        MIDDLE_ALIGNMENT = 1
        BOTTOM_ALIGNMENT = 2

    HORIZONTAL_ORIENTATION: ClassVar[Orientation]
    VERTICAL_ORIENTATION: ClassVar[Orientation]
    LEFT_ALIGNMENT: ClassVar[Alignment]
    CENTER_ALIGNMENT: ClassVar[Alignment]
    RIGHT_ALIGNMENT: ClassVar[Alignment]
    TOP_ALIGNMENT: ClassVar[Alignment]
    MIDDLE_ALIGNMENT: ClassVar[Alignment]
    BOTTOM_ALIGNMENT: ClassVar[Alignment]
    orientation: Orientation
    alignment: Alignment
    wrap: bool
    margin: Vec
    spacing: Vec

class Scrollbar(OpaqueWidget):
    vertical: bool

ScrollBar = Scrollbar  # deprecated; access warns

class ScrollWidget(OpaqueWidget):
    @property
    def container(self) -> HostWidget: ...
    @property
    def horizontal_scrollbar(self) -> Widget | HostWidget | None: ...
    @property
    def vertical_scrollbar(self) -> Widget | HostWidget | None: ...
    offset: Vec
    container_box: Rect
    hide_scrollbars: bool
    def get_scroll_offset(self) -> Vec: ...
    def scroll_to(self, r: Rect) -> None: ...
    def get_container_offset_bound(self) -> Rect: ...
    def get_handle_offset(self) -> Vec: ...
    def get_handle_size(self) -> Vec: ...
    def is_scrolling(self) -> bool: ...

class TextField(OpaqueWidget):
    text: str
    placeholder: str
    password: bool
    multiline: bool
    cursor: int
    selection: int
    prev_field: Widget | HostWidget | None
    next_field: Widget | HostWidget | None
    def get_text_position(self, mouse_pos: Vec) -> int: ...
    def get_text(self) -> str: ...
    def set_text(self, text: str) -> None: ...
    def select_all(self) -> None: ...
    def get_selected_text(self) -> str: ...
    def insert_text(self, text: str) -> None: ...
    def copy_clipboard(self) -> None: ...
    def cut_clipboard(self) -> None: ...
    def paste_clipboard(self) -> None: ...
    def cursor_to_prev_word(self) -> None: ...
    def cursor_to_next_word(self) -> None: ...
    def cursor_to_line_start(self) -> None: ...
    def cursor_to_line_end(self) -> None: ...
    def create_context_menu(self) -> None: ...

class PasswordField(TextField): ...

class MenuOverlay(OpaqueWidget):
    bg_color: NVGcolor

class Menu(OpaqueWidget):
    @property
    def parent_menu(self) -> Menu | None: ...  # Rack 2.6.6 never sets it
    @property
    def child_menu(self) -> Menu | None: ...
    @property
    def active_entry(self) -> MenuEntry | None: ...
    corner_flags: int
    def set_child_menu(self, menu: Menu | None) -> None: ...

class MenuEntry(OpaqueWidget): ...

class MenuItem(MenuEntry):
    text: str
    right_text: str
    disabled: bool
    def do_action(self, consume: bool = True) -> None: ...
    def create_child_menu(self) -> Menu | None: ...

class ColorDotMenuItem(MenuItem):
    color: NVGcolor

class MenuLabel(MenuEntry):
    text: str

class MenuSeparator(MenuEntry): ...
