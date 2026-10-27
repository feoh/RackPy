# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-26A: rack::ui widgets as Python classes, run in Rack's live UI."""

import gc
import threading
import weakref
from typing import Any

import pytest

from rackpy.errors import OwnershipError, StaleHandleError, ThreadError
from rackpy.raw import nanovg, ui
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec
from rackpy.types import ValueQuantity


def framed(*children: w.Widget) -> w.FramebufferWidget:
    frame = w.FramebufferWidget()
    frame.box = Rect(Vec(), Vec(200, 120))
    for child in children:
        frame.add_child(child)
    return frame


def test_classes_follow_racks_hierarchy() -> None:
    assert issubclass(ui.Button, w.OpaqueWidget) and issubclass(ui.OptionButton, ui.RadioButton)
    assert issubclass(ui.RadioButton, ui.Button) and issubclass(ui.ChoiceButton, ui.Button)
    assert issubclass(ui.MenuItem, ui.MenuEntry) and issubclass(ui.ColorDotMenuItem, ui.MenuItem)
    assert issubclass(ui.PasswordField, ui.TextField) and issubclass(ui.TooltipOverlay, w.TransparentWidget)
    assert issubclass(ui.ProgressBar, w.Widget) and not issubclass(ui.ProgressBar, w.OpaqueWidget)
    assert ui.ScrollBar is ui.Scrollbar
    assert ui.CHECKMARK_STRING == "✔" and ui.CHECKMARK(True) == "✔" and ui.CHECKMARK(False) == ""
    assert ui.RIGHT_ARROW == "▸"


def test_fields_round_trip() -> None:
    label, layout, tooltip = ui.Label(), ui.SequentialLayout(), ui.Tooltip()
    try:
        label.text, label.font_size, label.line_height = "λ Level", 14.0, 1.5
        label.color = nanovg.NVGcolor(10 / 255, 20 / 255, 30 / 255)
        label.alignment = ui.Label.CENTER_ALIGNMENT
        assert (label.text, label.font_size, label.line_height) == ("λ Level", 14.0, 1.5)
        assert label.alignment == ui.Label.Alignment.CENTER_ALIGNMENT and label.color.r == pytest.approx(10 / 255)
        for alignment in (ui.Label.Alignment.LEFT_ALIGNMENT, ui.Label.Alignment.RIGHT_ALIGNMENT):
            label.alignment = alignment
            assert label.alignment == alignment
        assert ui.Label.LEFT_ALIGNMENT != ui.Label.RIGHT_ALIGNMENT
        layout.orientation = ui.SequentialLayout.VERTICAL_ORIENTATION
        assert layout.orientation == ui.SequentialLayout.Orientation.VERTICAL_ORIENTATION
        layout.alignment = ui.SequentialLayout.MIDDLE_ALIGNMENT
        layout.wrap, layout.margin, layout.spacing = False, Vec(2, 3), Vec(4, 5)
        assert ui.SequentialLayout.MIDDLE_ALIGNMENT == ui.SequentialLayout.CENTER_ALIGNMENT  # Rack's aliases
        assert ui.SequentialLayout.TOP_ALIGNMENT == ui.SequentialLayout.LEFT_ALIGNMENT
        assert ui.SequentialLayout.BOTTOM_ALIGNMENT == ui.SequentialLayout.RIGHT_ALIGNMENT
        assert ui.SequentialLayout.LEFT_ALIGNMENT != ui.SequentialLayout.RIGHT_ALIGNMENT
        layout.orientation = ui.SequentialLayout.HORIZONTAL_ORIENTATION
        assert layout.orientation == ui.SequentialLayout.Orientation.HORIZONTAL_ORIENTATION
        assert (layout.wrap, layout.margin, layout.spacing) == (False, Vec(2, 3), Vec(4, 5))
        tooltip.text = "tip"
        assert tooltip.text == "tip"
    finally:
        for widget in (label, layout, tooltip):
            widget.close()


def test_sequential_layout_positions_its_children() -> None:
    layout = ui.SequentialLayout()
    a, b = w.Widget(), w.Widget()
    try:
        layout.box = Rect(Vec(), Vec(100, 100))
        a.box, b.box = Rect(Vec(), Vec(10, 10)), Rect(Vec(), Vec(20, 10))
        layout.add_child(a)
        layout.add_child(b)
        layout.spacing = Vec(5, 5)
        layout.step()  # Rack's SequentialLayout::step lays the children out
        assert a.get_position() == Vec(0, 0) and b.get_position() == Vec(15, 0)
    finally:
        layout.close()


def test_quantity_fields_keep_their_quantity_alive() -> None:
    button, slider, bar = ui.Button(), ui.Slider(), ui.ProgressBar()
    try:
        q = ValueQuantity(0, 1, 0)
        marker = weakref.ref(q)
        button.quantity = q
        slider.quantity = q
        bar.quantity = q
        del q
        gc.collect()
        assert marker() is not None and button.quantity is marker() and slider.quantity is marker()
        # Rack's Button: press sets the quantity to its maximum, release to its minimum.
        button.on_drag_start(w.DragStartEvent(button=0))
        assert marker().get_value() == 1  # type: ignore[union-attr]
        button.on_drag_end(w.DragEndEvent(button=0))
        assert marker().get_value() == 0  # type: ignore[union-attr]
        with pytest.raises(TypeError):
            button.quantity = "not a quantity"  # type: ignore[assignment]
        button.quantity = None
        assert button.quantity is None
    finally:
        for widget in (button, slider, bar):
            widget.close()
    gc.collect()
    assert marker() is None  # the widgets released it when Rack deleted them


def test_rack_draws_the_widgets() -> None:
    q = ValueQuantity(0, 10, 3, "Level", " V")
    label, button, slider, bar = ui.Label(), ui.Button(), ui.Slider(), ui.ProgressBar()
    field, item, separator, menu_label = ui.TextField(), ui.MenuItem(), ui.MenuSeparator(), ui.MenuLabel()
    label.text, button.text, field.text, item.text, menu_label.text = "Label", "Push", "typed", "Item", "Header"
    slider.quantity = bar.quantity = q
    layout = ui.SequentialLayout()
    layout.box = Rect(Vec(), Vec(200, 120))
    for child in (label, button, slider, bar, field, item, separator, menu_label):
        child.box = Rect(Vec(), Vec(60, 18))
        layout.add_child(child)
    frame = framed(layout)
    try:
        layout.step()
        frame.render()  # Rack's own draw() for each, through its framebuffer
        frame.render()
    finally:
        frame.close()


def next_of(field: ui.TextField) -> object:
    return field.next_field


def test_text_field_editing_and_focus_fields() -> None:
    field, other = ui.TextField(), ui.TextField()
    try:
        field.set_text("hello world")
        assert field.get_text() == "hello world" and field.cursor == len("hello world")
        field.select_all()
        assert field.get_selected_text() == "hello world"
        field.cursor, field.selection = 5, 0
        field.insert_text("λ")
        assert field.get_text() == "λ world"
        field.cursor = 999  # clamped: Rack indexes the text with it
        assert field.cursor == len("λ world".encode())
        field.placeholder, field.password, field.multiline = "type", True, True
        assert (field.placeholder, field.password, field.multiline) == ("type", True, True)
        field.next_field = other
        other.prev_field = field
        assert next_of(field) is other and other.prev_field is field
        other.close()
        assert next_of(field) is None  # Rack never keeps a pointer to a deleted widget
        field.prev_field = None
        assert field.prev_field is None
        assert isinstance(ui.PasswordField(), ui.TextField)
    finally:
        field.close()


def test_scroll_widget_container_and_offsets() -> None:
    scroll = ui.ScrollWidget()
    try:
        scroll.box = Rect(Vec(), Vec(50, 50))
        container = scroll.container
        assert isinstance(container, w.HostWidget) and container.parent is scroll
        content = w.Widget()
        content.box = Rect(Vec(), Vec(200, 300))
        container.add_child(content)  # the ScrollWidget's child now owns it
        assert content.parent is container
        scroll.step()
        scroll.scroll_to(Rect(Vec(150, 250), Vec(10, 10)))
        scroll.step()
        assert scroll.get_scroll_offset() == scroll.offset and scroll.offset.y > 0
        assert isinstance(scroll.get_container_offset_bound(), Rect) and not scroll.is_scrolling()
        assert scroll.vertical_scrollbar is not None and scroll.horizontal_scrollbar is not None
        scroll.hide_scrollbars = True
        assert scroll.hide_scrollbars
    finally:
        scroll.close()
    assert not content.alive


def test_menus_child_menus_and_actions() -> None:
    actions: list[str] = []

    class Item(ui.MenuItem):
        def on_action(self, event: Any) -> None:
            actions.append(self.text)
            super().on_action(event)

    class Parent(ui.MenuItem):
        def create_child_menu(self) -> ui.Menu:
            child = ui.Menu()  # built inside Rack's event: allowed, it is a new detached menu
            sub = Item()
            sub.text = "Sub"
            child.add_child(sub)
            return child

    overlay, menu = ui.MenuOverlay(), ui.Menu()
    overlay.bg_color = nanovg.NVGcolor(0, 0, 0, 0.5)
    first, parent = Item(), Parent()
    first.text, first.right_text, parent.text = "First", "Ctrl+F", "More"
    menu.add_child(first)
    menu.add_child(parent)
    overlay.add_child(menu)
    overlay.attach_to_scene()
    try:
        parent.on_enter(w.EnterEvent())  # Rack's MenuItem::onEnter asks for the child menu
        child = menu.child_menu
        assert isinstance(child, ui.Menu) and menu.active_entry is parent
        assert child.parent_menu is None  # Rack 2.6.6's setChildMenu does not set it
        assert child.parent is overlay and len(child.children) == 1
        first.do_action()
        assert actions == ["First"] and overlay.requested_delete  # a consumed action closes the menu
        first.disabled = True
        assert first.disabled and first.right_text == "Ctrl+F"
        replacement = ui.Menu()
        menu.set_child_menu(replacement)  # Rack deletes the previous child menu
        assert not child.alive and menu.child_menu is replacement
        with pytest.raises(OwnershipError, match="needs a parent"):
            ui.Menu().set_child_menu(None)
    finally:
        overlay.request_delete()


def test_bad_child_menus_are_contained() -> None:
    class Bad(ui.MenuItem):
        def create_child_menu(self) -> Any:
            return ui.Label()  # not a menu

    overlay, menu, item = ui.MenuOverlay(), ui.Menu(), Bad()
    menu.add_child(item)
    overlay.add_child(menu)
    overlay.attach_to_scene()
    try:
        item.on_enter(w.EnterEvent())
        assert menu.child_menu is None  # Rack got no menu; the error was logged
    finally:
        overlay.request_delete()


def test_theme_and_thread_rules() -> None:
    ui.set_theme(nanovg.NVGcolor(0.1, 0.1, 0.1), nanovg.NVGcolor(0.9, 0.9, 0.9))
    ui.refresh_theme()
    label = ui.Label()
    errors: list[BaseException] = []

    def worker() -> None:
        for action in (lambda: label.text, ui.refresh_theme, ui.Button):
            try:
                action()
            except BaseException as error:
                errors.append(error)

    try:
        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()
        assert len(errors) == 3 and all(isinstance(e, ThreadError) for e in errors)
    finally:
        label.close()
    with pytest.raises(StaleHandleError):
        label.text = "gone"
