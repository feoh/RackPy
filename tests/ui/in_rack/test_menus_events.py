# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-26B: Rack's context, input dispatch and menu helpers, run in Rack's live UI."""

import gc
import threading
from typing import Any

import _rackpy_host
import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.raw import macros, ui
from rackpy.raw import plugin as rp
from rackpy.raw import widget as w
from rackpy.raw.engine import Module
from rackpy.raw.math import Rect, Vec

GLFW_PRESS, GLFW_RELEASE = raw.glfw.GLFW_PRESS, raw.glfw.GLFW_RELEASE
GLFW_KEY_A, GLFW_MOUSE_BUTTON_LEFT = raw.glfw.GLFW_KEY_A, raw.glfw.GLFW_MOUSE_BUTTON_LEFT


@pytest.mark.parametrize(
    ("alias", "target"),
    [
        ("Base", "BaseEvent"),
        ("PositionBase", "PositionBaseEvent"),
        ("KeyBase", "KeyBaseEvent"),
        ("TextBase", "TextBaseEvent"),
        ("Hover", "HoverEvent"),
        ("Button", "ButtonEvent"),
        ("DoubleClick", "DoubleClickEvent"),
        ("HoverKey", "HoverKeyEvent"),
        ("HoverText", "HoverTextEvent"),
        ("HoverScroll", "HoverScrollEvent"),
        ("Enter", "EnterEvent"),
        ("Leave", "LeaveEvent"),
        ("Select", "SelectEvent"),
        ("Deselect", "DeselectEvent"),
        ("SelectKey", "SelectKeyEvent"),
        ("SelectText", "SelectTextEvent"),
        ("DragBase", "DragBaseEvent"),
        ("DragStart", "DragStartEvent"),
        ("DragEnd", "DragEndEvent"),
        ("DragMove", "DragMoveEvent"),
        ("DragHover", "DragHoverEvent"),
        ("DragEnter", "DragEnterEvent"),
        ("DragLeave", "DragLeaveEvent"),
        ("DragDrop", "DragDropEvent"),
        ("PathDrop", "PathDropEvent"),
        ("Action", "ActionEvent"),
        ("Change", "ChangeEvent"),
        ("Dirty", "DirtyEvent"),
        ("Reposition", "RepositionEvent"),
        ("Resize", "ResizeEvent"),
        ("Add", "AddEvent"),
        ("Remove", "RemoveEvent"),
        ("Show", "ShowEvent"),
        ("Hide", "HideEvent"),
    ],
)
def test_every_deprecated_event_alias_warns_and_preserves_identity(alias: str, target: str) -> None:
    with pytest.warns(DeprecationWarning, match=alias):
        assert getattr(raw.event, alias) is getattr(w, target)


class Pad(w.OpaqueWidget):
    """A target in Rack's scene that records what Rack dispatches to it."""

    def __init__(self) -> None:
        super().__init__()
        self.seen: list[tuple[str, Any]] = []

    def on_enter(self, event: Any) -> None:
        self.seen.append(("enter", None))
        super().on_enter(event)

    def on_leave(self, event: Any) -> None:
        self.seen.append(("leave", None))
        super().on_leave(event)

    def on_hover_key(self, event: Any) -> None:
        self.seen.append(("key", (event.key, event.action)))
        event.consume(self)

    def on_hover_text(self, event: Any) -> None:
        self.seen.append(("text", event.codepoint))
        event.consume(self)

    def on_hover_scroll(self, event: Any) -> None:
        self.seen.append(("scroll", event.scroll_delta))
        event.consume(self)

    def on_button(self, event: Any) -> None:
        self.seen.append(("button", (event.button, event.action)))
        super().on_button(event)

    def on_select(self, event: Any) -> None:
        self.seen.append(("select", None))

    def on_deselect(self, event: Any) -> None:
        self.seen.append(("deselect", None))


def same(a: object, b: object) -> bool:
    return a is b


def placed() -> Pad:
    pad = Pad()
    pad.box = Rect(Vec(200, 200), Vec(80, 80))
    pad.attach_to_scene()
    return pad


def test_context_and_event_state() -> None:
    context = raw.context_get()
    assert context is not None and context is raw.APP() and context.alive
    state = context.event
    assert isinstance(state, w.EventState) and state is context.event
    root = state.root_widget
    assert root is not None and root is state.get_root_widget()
    found: list[object] = []
    thread = threading.Thread(target=lambda: found.append(raw.context_get()))
    thread.start()
    thread.join()
    assert found == [None]  # no Rack context on a thread Python started


def test_rack_dispatches_input_to_python_widgets() -> None:
    context = raw.context_get()
    assert context is not None and context.event is not None
    state = context.event
    pad = placed()
    inside = Vec(240, 240)
    try:
        assert state.handle_hover(inside, Vec(1, 1))
        assert same(state.hovered_widget, pad) and ("enter", None) in pad.seen
        assert state.handle_key(inside, GLFW_KEY_A, 38, GLFW_PRESS, 0)
        assert GLFW_KEY_A in state.held_keys
        state.handle_key(inside, GLFW_KEY_A, 38, GLFW_RELEASE, 0)
        assert GLFW_KEY_A not in state.held_keys
        assert state.handle_text(inside, 0x1F642)
        assert state.handle_scroll(inside, Vec(0, -3))
        state.handle_button(inside, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0)
        state.handle_button(inside, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0)
        kinds = [kind for kind, _ in pad.seen]
        assert ("key", (GLFW_KEY_A, GLFW_PRESS)) in pad.seen and ("text", 0x1F642) in pad.seen
        assert ("scroll", Vec(0, -3)) in pad.seen and ("button", (0, GLFW_PRESS)) in pad.seen and "button" in kinds
        state.set_selected_widget(pad)
        assert same(state.selected_widget, pad) and ("select", None) in pad.seen
        state.set_selected_widget(None)
        assert ("deselect", None) in pad.seen
        state.handle_key(inside, GLFW_KEY_A, 38, GLFW_PRESS, 0)
        state.handle_leave()  # Rack's: forget held keys, but keep the hovered widget (the mouse may be dragging)
        assert not state.held_keys and same(state.hovered_widget, pad)
        state.set_hovered_widget(None)
        assert ("leave", None) in pad.seen
    finally:
        pad.remove_from_parent()
        pad.close()


def test_raw_fields_and_finalization() -> None:
    context = raw.context_get()
    assert context is not None and context.event is not None
    state = context.event
    pad = placed()
    try:
        state.hovered_widget = pad  # the raw field: no Enter event
        assert same(state.hovered_widget, pad) and not pad.seen
        state.drag_button, state.last_click_time = 1, 12.5
        assert (state.drag_button, state.last_click_time) == (1, 12.5)
        state.finalize_widget(pad)
        assert same(state.hovered_widget, None)
        state.selected_widget = pad
        pad.remove_from_parent()  # Rack's removeChild finalizes the widget in the event state
        assert same(state.selected_widget, None)
        with pytest.warns(DeprecationWarning, match="set_hovered_widget"):
            state.set_hovered(None)
        with pytest.raises(AttributeError):
            state.root_widget = pad  # type: ignore[misc,unused-ignore]
    finally:
        if pad.parent is not None:
            pad.remove_from_parent()
        pad.close()


def test_event_state_drag_focus_drop_and_dirty() -> None:
    calls: list[tuple[str, object]] = []

    class Target(w.OpaqueWidget):
        def on_drag_start(self, event: Any) -> None:
            calls.append(("start", event.button))

        def on_drag_end(self, event: Any) -> None:
            calls.append(("end", event.button))

        def on_drag_enter(self, event: Any) -> None:
            calls.append(("drag-enter", event.origin))

        def on_drag_leave(self, event: Any) -> None:
            calls.append(("drag-leave", event.origin))

        def on_path_drop(self, event: Any) -> None:
            calls.append(("drop", event.paths))
            event.consume(self)

        def on_dirty(self, event: Any) -> None:
            calls.append(("dirty", None))

    context = raw.context_get()
    assert context is not None and context.event is not None
    state = context.event
    target = Target()
    target.box = Rect(Vec(200, 200), Vec(80, 80))
    target.attach_to_scene()
    try:
        state.set_dragged_widget(target, 2)
        assert state.dragged_widget is target and state.get_dragged_widget() is target
        assert ("start", 2) in calls
        state.set_drag_hovered_widget(target)
        assert state.drag_hovered_widget is target and state.get_drag_hovered_widget() is target
        assert ("drag-enter", target) in calls
        state.set_drag_hovered_widget(None)
        assert state.get_drag_hovered_widget() is None and ("drag-leave", target) in calls
        state.set_dragged_widget(None, 2)
        assert state.get_dragged_widget() is None and ("end", 2) in calls
        state.set_hovered_widget(target)
        assert state.get_hovered_widget() is target
        state.set_selected_widget(target)
        assert state.get_selected_widget() is target
        state.last_clicked_widget = target
        assert state.last_clicked_widget is target
        assert state.handle_drop(Vec(240, 240), ["/tmp/rackpy-input.svg"])
        assert ("drop", ["/tmp/rackpy-input.svg"]) in calls
        state.handle_dirty()
        assert ("dirty", None) in calls
        for name, args in (("set_dragged", (target, 1)), ("set_drag_hovered", (target,)), ("set_selected", (target,))):
            with pytest.warns(DeprecationWarning):
                getattr(state, name)(*args)
    finally:
        state.set_drag_hovered_widget(None)
        state.set_dragged_widget(None, 1)
        state.set_selected_widget(None)
        state.set_hovered_widget(None)
        state.last_clicked_widget = None
        target.remove_from_parent()
        target.close()


def test_menu_helpers_build_working_menus() -> None:
    calls: list[str] = []
    flag = raw.Ref(False)
    mode = raw.Ref(1)
    state = {"loop": False, "index": 0}

    class Tinted(ui.MenuItem):
        pass

    class CustomMenu(ui.Menu):
        pass

    class CustomLabel(ui.MenuLabel):
        pass

    menu = raw.create_menu(CustomMenu)
    assert isinstance(menu, CustomMenu)
    overlay = menu.parent
    assert isinstance(overlay, ui.MenuOverlay)
    try:
        label: ui.MenuLabel = raw.create_menu_label("Header", label_type=CustomLabel)
        plain = raw.create_menu_item("Plain", "R", item_type=Tinted)
        act = raw.create_menu_item("Act", "", lambda: calls.append("act"))
        check = raw.create_check_menu_item("Check", "x", lambda: True, lambda: calls.append("check"), item_type=Tinted)

        def set_loop(value: bool) -> None:
            state["loop"] = value

        toggle = raw.create_bool_menu_item("Loop", "", lambda: bool(state["loop"]), set_loop, item_type=Tinted)
        pointer = raw.create_bool_ptr_menu_item("Flag", "", flag)
        submenu = raw.create_submenu_item(
            "More", "", lambda m: m.add_child(raw.create_menu_item("Inner", "")), item_type=Tinted
        )
        index = raw.create_index_submenu_item(
            "Mode", ["A", "B", "C"], lambda: int(state["index"]), lambda i: state.update(index=i), item_type=Tinted
        )
        index_ptr = raw.create_index_ptr_submenu_item("Ptr", ["X", "Y"], mode)
        for item in (label, plain, act, check, toggle, pointer, submenu, index, index_ptr):
            menu.add_child(item)
        assert isinstance(label, CustomLabel) and label.text == "Header"
        assert all(isinstance(item, Tinted) for item in (plain, check, toggle, submenu, index))
        assert plain.right_text == "R"
        menu.step()  # Rack's step: check marks and index labels update
        assert check.right_text == "x  " + ui.CHECKMARK_STRING and index.right_text == "A  " + ui.RIGHT_ARROW
        assert submenu.right_text == ui.RIGHT_ARROW
        act.do_action(False)  # not consumed: the menu stays open for the next items
        check.do_action(False)
        toggle.do_action(False)
        pointer.do_action(False)
        assert calls == ["act", "check"] and state["loop"] is True and flag.value is True
        submenu.on_enter(w.EnterEvent())
        inner = menu.child_menu
        assert inner is not None and len(inner.children) == 1
        index.on_enter(w.EnterEvent())
        choices = menu.child_menu
        assert choices is not None and len(choices.children) == 3
        second = choices.children[1]
        assert isinstance(second, ui.MenuItem)
        second.do_action(False)
        assert state["index"] == 1
        index_ptr.on_enter(w.EnterEvent())
        assert menu.child_menu is not None
        first_choice = menu.child_menu.children[0]
        assert isinstance(first_choice, ui.MenuItem)
        first_choice.do_action(False)
        assert mode.value == 0
        assert raw.create_bool_ptr_menu_item("Null", "", None) is not None  # a null pointer reads false
        with pytest.raises(TypeError, match="item_type"):
            raw.create_menu_item("Bad", "", item_type=ui.Label)  # type: ignore[arg-type]
    finally:
        overlay.request_delete()


def test_a_menu_outliving_its_module_fails_safely() -> None:
    module = Module.create(lambda m: m.config(1, 0, 0, 0))
    HOOKS.engine_adopt(module)
    params = module.params

    def read() -> None:
        params[0].get_value()

    menu = raw.create_menu()
    overlay = menu.parent
    assert isinstance(overlay, ui.MenuOverlay)
    item = raw.create_menu_item("Reset", "", read)
    menu.add_child(item)
    HOOKS.engine_delete(module, None)
    del module
    gc.collect()
    try:
        raised = _rackpy_host.dispatch_stats()["raised"]
        item.do_action()  # the callback's module is gone: StaleHandleError, contained
        assert _rackpy_host.dispatch_stats()["raised"] == raised + 1
    finally:
        if overlay.alive:
            overlay.request_delete()


def test_model_context_menu_and_deprecated_aliases() -> None:
    probe = rp.get_model("RackPy", "RuntimeProbe")
    assert probe is not None
    menu = ui.Menu()
    try:
        probe.append_context_menu(menu)
        assert len(menu.children) > 0  # Rack's own items for the model
    finally:
        menu.close()
    with pytest.warns(DeprecationWarning, match="rack::event::Action"):
        assert raw.event.Action is w.ActionEvent
    with pytest.warns(DeprecationWarning):
        assert raw.event.Hover is w.HoverEvent
    assert macros.RACK_MOD_CTRL == 2 and macros.RACK_MOD_CTRL_NAME == "Ctrl" and macros.RACK_MOD_SHIFT == 1
    assert macros.RACK_MOD_MASK == 15 and macros.RACK_HELD == 3 and macros.RACK_MOD_ALT_NAME == "Alt"
