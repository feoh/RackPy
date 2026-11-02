# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-42: what Rack's own ui widget code does, asserted through the Python classes (widgets-09 review gaps)."""

from typing import Any

import pytest

from rackpy.raw import glfw, nanovg, ui
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec
from rackpy.types import ValueQuantity


def closing(overlay: ui.MenuOverlay) -> bool:
    return bool(overlay.requested_delete)  # a function, so a mypy assert on it does not narrow the next read


def widget_width(widget: Any) -> float:
    widget.step()
    return float(widget.box.size.x)


def test_text_and_colour_fields_read_back() -> None:
    button, menu_label, item, dot, overlay = (
        ui.Button(),
        ui.MenuLabel(),
        ui.MenuItem(),
        ui.ColorDotMenuItem(),
        ui.MenuOverlay(),
    )
    try:
        button.text, menu_label.text, item.text, item.right_text = "Push", "Header", "Item", "Ctrl+I"
        dot.color = nanovg.NVGcolor(0.25, 0.5, 0.75, 1.0)
        overlay.bg_color = nanovg.NVGcolor(0.1, 0.2, 0.3, 0.4)
        assert (button.text, menu_label.text, (item.text, item.right_text)) == ("Push", "Header", ("Item", "Ctrl+I"))
        assert (item.disabled, ui.MenuItem().disabled) == (False, False)
        assert (dot.color.r, dot.color.g, dot.color.b, dot.color.a) == pytest.approx((0.25, 0.5, 0.75, 1.0))
        assert (overlay.bg_color.r, overlay.bg_color.a) == pytest.approx((0.1, 0.4))
        layout = ui.SequentialLayout()
        layout.alignment = ui.SequentialLayout.MIDDLE_ALIGNMENT
        assert layout.alignment == ui.SequentialLayout.MIDDLE_ALIGNMENT
        layout.alignment = ui.SequentialLayout.RIGHT_ALIGNMENT
        assert layout.alignment == ui.SequentialLayout.RIGHT_ALIGNMENT
        layout.close()
    finally:
        for widget in (button, menu_label, item, dot, overlay):
            widget.close()


def test_menu_item_widths_follow_racks_step() -> None:
    # MenuItem::step: label width (+ right text width - 10) + 10. MenuLabel::step: label width + 10.
    # ColorDotMenuItem::step: MenuItem::step, then 20 more. The font measure is Rack's, so compare the widths.
    plain, with_right, dot, label, longer = (
        ui.MenuItem(),
        ui.MenuItem(),
        ui.ColorDotMenuItem(),
        ui.MenuLabel(),
        ui.MenuItem(),
    )
    try:
        plain.text = with_right.text = dot.text = label.text = "Item"
        longer.text = "A considerably longer item"
        with_right.right_text = "Ctrl+I"
        base = widget_width(plain)
        assert base > 10 and widget_width(longer) > base  # width grows with the text
        assert widget_width(with_right) > base - 10 + 10 and widget_width(with_right) != base  # right text counts
        assert widget_width(dot) == pytest.approx(base + 20)
        assert widget_width(label) == pytest.approx(base)  # both: label width + 10
    finally:
        for widget in (plain, with_right, dot, label, longer):
            widget.close()


def test_scroll_container_offset_bound_is_the_overflow_of_the_container() -> None:
    scroll = ui.ScrollWidget()
    try:
        scroll.box = Rect(Vec(), Vec(50, 40))
        content = w.Widget()
        content.box = Rect(Vec(), Vec(200, 300))
        scroll.container.add_child(content)
        scroll.step()
        bound, container_box = scroll.get_container_offset_bound(), scroll.container_box
        assert bound.pos == container_box.pos
        assert bound.size == container_box.size.minus(scroll.box.size)
        assert bound.size.x > 0 and bound.size.y > 0  # the content is larger than the view
    finally:
        scroll.close()


def test_menu_hover_scroll_moves_a_menu_that_does_not_fit_its_parent() -> None:
    # Menu::onHoverScroll: box.pos.y += scrollDelta.y, only while the parent's box does not contain the menu's.
    overlay, menu = ui.MenuOverlay(), ui.Menu()
    overlay.box = Rect(Vec(), Vec(100, 100))
    menu.box = Rect(Vec(10, 10), Vec(40, 40))
    overlay.add_child(menu)
    try:
        menu.on_hover_scroll(w.HoverScrollEvent(scroll_delta=Vec(0, 7), context=w.EventContext()))
        assert menu.box.pos == Vec(10, 10)  # it fits: unchanged
        menu.box = Rect(Vec(10, 10), Vec(40, 400))
        menu.on_hover_scroll(w.HoverScrollEvent(scroll_delta=Vec(0, -25), context=w.EventContext()))
        assert menu.box.pos == Vec(10, -15)
    finally:
        overlay.close()


def test_menu_overlay_keys_buttons_and_actions() -> None:
    key = glfw.GLFW_KEY_ESCAPE
    overlay, other = ui.MenuOverlay(), ui.MenuOverlay()
    third = ui.MenuOverlay()
    try:
        for widget in (overlay, other, third):
            widget.box = Rect(Vec(), Vec(100, 100))
        letter = w.HoverKeyEvent(key=glfw.GLFW_KEY_A, action=glfw.GLFW_PRESS, context=w.EventContext())
        overlay.on_hover_key(letter)
        assert letter.is_consumed() and not closing(overlay)  # every key is consumed, only Escape closes it
        escape = w.HoverKeyEvent(key=key, action=glfw.GLFW_PRESS, context=w.EventContext())
        overlay.on_hover_key(escape)
        assert escape.is_consumed() and closing(overlay)
        released = w.HoverKeyEvent(key=key, action=glfw.GLFW_RELEASE, context=w.EventContext())
        other.on_hover_key(released)
        assert released.is_consumed() and not closing(other)  # a release does not close it
        press = w.ButtonEvent(button=glfw.GLFW_MOUSE_BUTTON_LEFT, action=glfw.GLFW_PRESS, context=w.EventContext())
        other.on_button(press)  # consume all buttons; a press is an action: the overlay is deleted
        assert press.is_consumed() and closing(other)
        third.on_action(w.ActionEvent())
        assert closing(third)  # Rack's MenuOverlay::onAction is requestDelete
    finally:
        for widget in (overlay, other, third):
            widget.close()


def test_radio_button_drags_keep_the_quantity_and_drag_drop_toggles_it() -> None:
    # RadioButton::onDragStart/End are OpaqueWidget's: unlike Button, they do not set the quantity. onDragDrop toggles.
    q = ValueQuantity(0, 1, 0, "Option")
    radio, option, choice = ui.RadioButton(), ui.OptionButton(), ui.ChoiceButton()
    try:
        radio.quantity = q
        radio.on_drag_start(w.DragStartEvent(button=glfw.GLFW_MOUSE_BUTTON_LEFT, context=w.EventContext()))
        assert q.get_value() == 0
        radio.on_drag_end(w.DragEndEvent(button=glfw.GLFW_MOUSE_BUTTON_LEFT, context=w.EventContext()))
        assert q.get_value() == 0
        drop = w.DragDropEvent(origin=radio, context=w.EventContext())
        radio.on_drag_drop(drop)
        assert q.get_value() == 1
        radio.on_drag_drop(w.DragDropEvent(origin=radio, context=w.EventContext()))
        assert q.get_value() == 0  # toggled back
        radio.on_drag_drop(w.DragDropEvent(origin=option, context=w.EventContext()))
        assert q.get_value() == 0  # another origin: ignored
        assert option.text == "" and choice.text == ""
    finally:
        for widget in (radio, option, choice):
            widget.close()
