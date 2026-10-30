# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-26A: rack::ui widgets as Python classes, run in Rack's live UI."""

import gc
import threading
import weakref
from typing import Any

import pytest

from rackpy import raw
from rackpy.errors import OwnershipError, StaleHandleError, ThreadError
from rackpy.raw import Quantity, glfw, nanovg, ui
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


def test_list_positions_children_vertically() -> None:
    layout = ui.List()
    first, second = w.Widget(), w.Widget()
    try:
        first.box, second.box = Rect(Vec(), Vec(10, 12)), Rect(Vec(), Vec(20, 18))
        layout.add_child(first)
        layout.add_child(second)
        layout.step()
        assert first.get_position() == Vec(0, 0) and second.get_position() == Vec(0, 12)
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


def test_button_radio_and_menu_drop_actions() -> None:
    calls: list[str] = []

    class Button(ui.Button):
        def on_action(self, event: Any) -> None:
            calls.append("button")

    class Radio(ui.RadioButton):
        def on_action(self, event: Any) -> None:
            calls.append("radio")

    class Item(ui.MenuItem):
        def on_action(self, event: Any) -> None:
            calls.append("item")

    button, radio, item, other = Button(), Radio(), Item(), w.Widget()
    quantity = ValueQuantity(0, 1, 0)
    try:
        radio.quantity = quantity
        for widget in (button, radio, item):
            widget.on_drag_drop(w.DragDropEvent(origin=other))
        assert calls == [] and quantity.get_value() == 0
        button.on_drag_drop(w.DragDropEvent(origin=button))
        radio.on_drag_drop(w.DragDropEvent(origin=radio))
        item.on_drag_drop(w.DragDropEvent(origin=item))
        assert calls == ["button", "radio", "item"] and quantity.get_value() == 1
        item.disabled = True
        item.on_drag_drop(w.DragDropEvent(origin=item))
        assert calls == ["button", "radio", "item"]
    finally:
        for owned in (button, radio, item, other):
            owned.close()


def test_slider_double_click_resets_quantity() -> None:
    slider = ui.Slider()
    quantity = ValueQuantity(0, 10, 3)
    try:
        slider.quantity = quantity
        quantity.set_value(8)
        slider.on_double_click(w.DoubleClickEvent())
        assert quantity.get_value() == 3
        slider.on_drag_move(w.DragMoveEvent(mouse_delta=Vec(100, 0)))
        assert quantity.get_value() == pytest.approx(4)
    finally:
        slider.close()


def test_slider_dispatches_plain_quantity_value_overrides() -> None:
    calls: list[float] = []

    class Custom(Quantity):
        def __init__(self) -> None:
            super().__init__()
            self.value = 0.25

        def get_value(self) -> float:
            return self.value

        def set_value(self, value: float) -> None:
            calls.append(value)
            self.value = value

        def get_min_value(self) -> float:
            return -1.0

        def get_max_value(self) -> float:
            return 3.0

        def get_default_value(self) -> float:
            return 1.5

    quantity, slider = Custom(), ui.Slider()
    slider.box = Rect(Vec(), Vec(100, 20))
    slider.quantity = quantity
    try:
        slider.on_drag_move(w.DragMoveEvent(button=0, mouse_delta=Vec(25, 0)))
        assert calls and quantity.value != 0.25
        assert quantity.get_range() == 4 and quantity.is_bounded()
        quantity.reset()
        assert quantity.value == 1.5
    finally:
        slider.close()


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


def test_text_cursor_clipboard_and_selected_input() -> None:
    field = ui.TextField()
    try:
        field.set_text("alpha beta")
        field.cursor_to_line_start()
        assert field.cursor == 0
        field.cursor_to_next_word()
        assert 0 < field.cursor < len(field.text)
        field.cursor_to_line_end()
        assert field.cursor == len(field.text)
        field.cursor_to_prev_word()
        assert 0 < field.cursor < len(field.text)
        field.select_all()
        field.copy_clipboard()
        field.cut_clipboard()
        assert field.get_text() == ""
        field.paste_clipboard()
        assert field.get_text() == "alpha beta"
        field.select_all()
        field.on_select_text(w.SelectTextEvent(codepoint=ord("Z"), context=w.EventContext()))
        assert field.get_text() == "Z"
        key = w.SelectKeyEvent(key=glfw.GLFW_KEY_BACKSPACE, action=glfw.GLFW_PRESS, context=w.EventContext())
        field.on_select_key(key)
        assert field.get_text() == "" and key.is_consumed()
    finally:
        field.close()


def test_text_field_position_override_drives_drag_selection() -> None:
    class Field(ui.TextField):
        def get_text_position(self, mouse_pos: Vec) -> int:
            return 2

    field = Field()
    try:
        field.set_text("abcd")
        field.on_drag_hover(w.DragHoverEvent(origin=field, pos=Vec(5, 5), context=w.EventContext()))
        assert field.cursor == 2 and field.selection == 4
        plain = ui.TextField()
        try:
            plain.set_text("abcd")
            assert 0 <= plain.get_text_position(Vec(0, 0)) <= 4
        finally:
            plain.close()
    finally:
        field.close()


def test_text_field_context_menu_intercepts_clicks() -> None:
    hits: list[int] = []

    class Target(w.OpaqueWidget):
        def on_button(self, event: Any) -> None:
            hits.append(event.action)
            event.consume(self)

    context = raw.context_get()
    assert context is not None and context.event is not None and context.scene is not None
    state = context.event
    field, target = ui.TextField(), Target()
    target.box = Rect(Vec(200, 200), Vec(80, 80))
    target.attach_to_scene()
    try:
        state.handle_button(Vec(240, 240), glfw.GLFW_MOUSE_BUTTON_LEFT, glfw.GLFW_PRESS, 0)
        assert hits == [glfw.GLFW_PRESS]
        hits.clear()
        context.scene.mouse_pos = Vec(20, 20)
        field.create_context_menu()
        state.handle_button(Vec(240, 240), glfw.GLFW_MOUSE_BUTTON_LEFT, glfw.GLFW_PRESS, 0)
        assert hits == []  # Rack's menu overlay intercepts this outside click
        context.scene.step()  # delete the dismissed overlay
        state.handle_button(Vec(240, 240), glfw.GLFW_MOUSE_BUTTON_LEFT, glfw.GLFW_PRESS, 0)
        assert hits == [glfw.GLFW_PRESS]
    finally:
        target.remove_from_parent()
        target.close()
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
        assert scroll.container_box.size.x > 0 and scroll.container_box.size.y > 0
        handle_size, handle_offset = scroll.get_handle_size(), scroll.get_handle_offset()
        assert 0 < handle_size.x < 1 and 0 < handle_size.y < 1
        assert 0 <= handle_offset.x <= 1 and 0 <= handle_offset.y <= 1
        before = scroll.offset
        scroll.on_drag_move(w.DragMoveEvent(mouse_delta=Vec(4, 6)))
        assert scroll.offset == before.minus(Vec(4, 6))
        scroll.on_hover_scroll(w.HoverScrollEvent(scroll_delta=Vec(0, -5), context=w.EventContext()))
        assert scroll.is_scrolling()
        scroll.on_hover(w.HoverEvent(mouse_delta=Vec(1, 0), context=w.EventContext()))
        assert not scroll.is_scrolling()
        scroll.hide_scrollbars = True
        assert scroll.hide_scrollbars
    finally:
        scroll.close()
    assert not content.alive


def test_scrollbar_drag_uses_its_orientation() -> None:
    scroll, bar = ui.ScrollWidget(), ui.Scrollbar()
    try:
        scroll.box = Rect(Vec(), Vec(50, 50))
        scroll.container_box = Rect(Vec(), Vec(200, 300))
        bar.box = Rect(Vec(), Vec(100, 100))
        scroll.add_child(bar)
        bar.vertical = True
        assert bar.vertical
        bar.on_drag_move(w.DragMoveEvent(mouse_delta=Vec(7, 10)))
        assert scroll.offset.x == 0 and scroll.offset.y == pytest.approx(30)
        scroll.offset = Vec()
        bar.vertical = False
        assert not bar.vertical
        bar.on_drag_move(w.DragMoveEvent(mouse_delta=Vec(10, 7)))
        assert scroll.offset.y == 0 and scroll.offset.x == pytest.approx(20)
    finally:
        scroll.close()


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
