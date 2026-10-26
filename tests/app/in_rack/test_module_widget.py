# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-28B: rackpy.raw.app.ModuleWidget without a module (as in a module browser preview), in Rack's live UI.

Module widgets with modules, in Rack's rack, are tested in windowed Rack
(tests/app/test_module_browser.py), where a Python model's widget factory
makes them.
"""

from collections.abc import Iterator

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.errors import OwnershipError
from rackpy.raw import app, ui
from rackpy.raw import widget as w
from rackpy.raw.engine import Module
from rackpy.raw.math import Rect, Vec
from rackpy.raw.window import Svg

PANEL = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="44" height="380">'
    '<rect width="44" height="380" fill="#30363f"/></svg>'
)


def model_of(widget: app.ModuleWidget) -> object:
    return widget.model  # read again: mypy would narrow the earlier None


@pytest.fixture
def widget() -> Iterator[app.ModuleWidget]:
    mw = app.ModuleWidget()
    yield mw
    if mw.alive:
        mw.close()


def test_a_module_widget_takes_its_module_only_from_its_factory(widget: app.ModuleWidget) -> None:
    assert widget.module is None and widget.model is None and widget.get_module() is None
    widget.set_module(None)  # a browser preview has none
    m = Module.create(lambda m: m.config(1, 0, 0, 0))
    HOOKS.engine_adopt(m)
    try:
        with pytest.raises(OwnershipError, match="widget factory"):
            widget.set_module(m)  # Rack deletes a module widget's module with it
        with pytest.warns(DeprecationWarning, match="set_module"), pytest.raises(OwnershipError):
            app.ModuleWidget(m)
        with pytest.warns(DeprecationWarning):
            app.ModuleWidget(None).close()
    finally:
        HOOKS.engine_delete(m, None)
    model = raw.plugin.get_model("RackPy", "RuntimeProbe")
    assert model is not None
    widget.set_model(model)
    assert widget.get_model() is model and model_of(widget) is model
    with pytest.raises(OwnershipError):
        widget.set_model(model)  # Rack's setModel asserts it is set once


def test_panels(widget: app.ModuleWidget) -> None:
    panel_svg = Svg()
    panel_svg.load_string(PANEL)
    widget.set_panel(panel_svg)
    first = widget.get_panel()
    assert isinstance(first, w.HostWidget)  # Rack's own SvgPanel
    assert widget.box.size.x == 45  # snapped to the rack grid (3 HP)
    with pytest.raises(ValueError, match="no image loaded"):
        widget.set_panel(Svg())
    mine = app.SvgPanel()
    mine.set_background(panel_svg)
    widget.set_panel(mine)
    assert widget.get_panel() is mine and mine.parent is widget and not first.alive  # Rack deleted the old one
    widget.set_panel(None)
    assert widget.get_panel() is None and not mine.alive
    blank = w.Widget()
    blank.box = Rect(Vec(), Vec(0, 380))
    widget.set_panel(blank)
    assert widget.box.size.x == 12 * app.RACK_GRID_WIDTH  # Rack's sanity width for a zero-width panel


def test_controls_and_scans(widget: app.ModuleWidget) -> None:
    knob = raw.create_param(app.Knob, Vec(1, 1), None, 3)
    inp = raw.create_input(app.PortWidget, Vec(1, 40), None, 0)
    out = raw.create_output(app.PortWidget, Vec(1, 80), None, 0)
    widget.add_param(knob)
    widget.add_input(inp)
    widget.add_output(out)
    with pytest.raises(ValueError, match="input port"):
        widget.add_input(raw.create_output(app.PortWidget, Vec(), None, 1))
    with pytest.raises(ValueError, match="already has input 0"):
        widget.add_input(raw.create_input(app.PortWidget, Vec(), None, 0))
    with pytest.raises(TypeError):
        widget.add_param(w.Widget())  # type: ignore[arg-type]
    assert widget.get_param(3) is knob and widget.get_param(4) is None
    assert widget.get_input(0) is inp and widget.get_output(0) is out
    assert widget.get_params() == [knob] and widget.get_ports() == [inp, out]
    assert widget.get_inputs() == [inp] and widget.get_outputs() == [out]
    assert knob.parent is widget


def test_grid_helpers(widget: app.ModuleWidget) -> None:
    widget.box = Rect(Vec(), Vec(60, 380))
    widget.set_grid_position(Vec(2, 1))
    assert widget.get_grid_position() == Vec(2, 1)
    assert widget.box.pos == app.RACK_OFFSET.plus(Vec(30, 380))
    assert widget.get_grid_size() == Vec(4, 1) and widget.get_grid_box() == Rect(Vec(2, 1), Vec(4, 1))


def test_actions_need_a_module_or_the_rack(widget: app.ModuleWidget) -> None:
    for action in (widget.to_json, widget.copy_clipboard, widget.reset_action, widget.remove_action):
        with pytest.raises(OwnershipError, match="module"):
            action()
    with pytest.raises(OwnershipError, match="model"):
        widget.has_template()
    menu = ui.Menu()
    try:
        widget.append_context_menu(menu)  # Rack's default adds nothing
        assert len(menu.children) == 0
    finally:
        menu.close()
