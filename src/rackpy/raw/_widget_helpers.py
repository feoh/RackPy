# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's widget helper templates (helpers.hpp), as Python factories (RP-28A).

``createParam<TParamWidget>(pos, module, paramId)`` and its siblings are C++
templates; here the template parameter is the first argument, a class (a
Python subclass is fine), and each factory sets the same fields in the same
order as Rack's. The module must be a RackPy module in Rack's engine, or
None; its IDs are checked (see :mod:`rackpy.raw.app`).
"""

from rackpy.raw import app as _app
from rackpy.raw import widget as _widget
from rackpy.raw import window as _window
from rackpy.raw.engine import Module as _Module
from rackpy.raw.engine import Port as _Port
from rackpy.raw.math import Vec as _Vec

__all__ = [
    "create_input",
    "create_input_centered",
    "create_light",
    "create_light_centered",
    "create_output",
    "create_output_centered",
    "create_panel",
    "create_param",
    "create_param_centered",
    "create_widget",
    "create_widget_centered",
]


def _check[T](widget_type: type[T], base: type, what: str) -> type[T]:
    if not (isinstance(widget_type, type) and issubclass(widget_type, base)):
        raise TypeError(f"{what} must be a {base.__module__}.{base.__qualname__} subclass")
    return widget_type


def _centered[T: _widget.Widget](widget: T) -> T:
    widget.box.pos = widget.box.pos.minus(widget.box.size.div(2))
    return widget


def create_widget[T: _widget.Widget](widget_type: type[T], pos: _Vec) -> T:
    """Rack's ``createWidget<TWidget>(pos)``: a new widget at ``pos``."""
    widget = _check(widget_type, _widget.Widget, "widget_type")()
    widget.box.pos = pos
    return widget


def create_widget_centered[T: _widget.Widget](widget_type: type[T], pos: _Vec) -> T:
    """Rack's ``createWidgetCentered<TWidget>(pos)``: a new widget centred on ``pos``."""
    return _centered(create_widget(widget_type, pos))


def create_panel(
    svg_path: str, dark_svg_path: str | None = None, *, panel_type: type[_app.SvgPanel] | None = None
) -> _app.SvgPanel:
    """Rack's ``createPanel<TPanel>(svgPath)``, or with ``dark_svg_path`` its themed overload.

    The SVGs load through Rack's cache (``Svg.load``). ``panel_type`` defaults to
    ``SvgPanel``, or ``ThemedSvgPanel`` with a dark SVG, as Rack's template defaults do.
    """
    if dark_svg_path is None:
        panel = _check(panel_type or _app.SvgPanel, _app.SvgPanel, "panel_type")()
        panel.set_background(_window.Svg.load(svg_path))
        return panel
    cls = panel_type or _app.ThemedSvgPanel
    if not (isinstance(cls, type) and issubclass(cls, _app.ThemedSvgPanel)):
        raise TypeError("panel_type must be a rackpy.raw.app.ThemedSvgPanel subclass when a dark SVG is given")
    themed = cls()
    themed.set_background(_window.Svg.load(svg_path), _window.Svg.load(dark_svg_path))
    return themed


def create_param[T: _app.ParamWidget](param_type: type[T], pos: _Vec, module: _Module | None, param_id: int) -> T:
    """Rack's ``createParam<TParamWidget>(pos, module, paramId)``, then ``init_param_quantity()``."""
    widget = _check(param_type, _app.ParamWidget, "param_type")()
    widget.box.pos = pos
    widget.param_id = param_id
    widget.module = module
    widget.init_param_quantity()
    return widget


def create_param_centered[T: _app.ParamWidget](
    param_type: type[T], pos: _Vec, module: _Module | None, param_id: int
) -> T:
    """Rack's ``createParamCentered``: ``create_param`` centred on ``pos``."""
    return _centered(create_param(param_type, pos, module, param_id))


def _port[T: _app.PortWidget](
    port_type: type[T], pos: _Vec, module: _Module | None, kind: _Port.Type, port_id: int
) -> T:
    widget = _check(port_type, _app.PortWidget, "port_type")()
    widget.box.pos = pos
    widget.type = kind
    widget.port_id = port_id
    widget.module = module
    return widget


def create_input[T: _app.PortWidget](port_type: type[T], pos: _Vec, module: _Module | None, input_id: int) -> T:
    """Rack's ``createInput<TPortWidget>(pos, module, inputId)``."""
    return _port(port_type, pos, module, _Port.Type.INPUT, input_id)


def create_input_centered[T: _app.PortWidget](
    port_type: type[T], pos: _Vec, module: _Module | None, input_id: int
) -> T:
    """Rack's ``createInputCentered``: ``create_input`` centred on ``pos``."""
    return _centered(create_input(port_type, pos, module, input_id))


def create_output[T: _app.PortWidget](port_type: type[T], pos: _Vec, module: _Module | None, output_id: int) -> T:
    """Rack's ``createOutput<TPortWidget>(pos, module, outputId)``."""
    return _port(port_type, pos, module, _Port.Type.OUTPUT, output_id)


def create_output_centered[T: _app.PortWidget](
    port_type: type[T], pos: _Vec, module: _Module | None, output_id: int
) -> T:
    """Rack's ``createOutputCentered``: ``create_output`` centred on ``pos``."""
    return _centered(create_output(port_type, pos, module, output_id))


def create_light[T: _app.ModuleLightWidget](
    light_type: type[T], pos: _Vec, module: _Module | None, first_light_id: int
) -> T:
    """Rack's ``createLight<TModuleLightWidget>(pos, module, firstLightId)``."""
    widget = _check(light_type, _app.ModuleLightWidget, "light_type")()
    widget.box.pos = pos
    widget.first_light_id = first_light_id
    widget.module = module
    return widget


def create_light_centered[T: _app.ModuleLightWidget](
    light_type: type[T], pos: _Vec, module: _Module | None, first_light_id: int
) -> T:
    """Rack's ``createLightCentered``: ``create_light`` centred on ``pos``."""
    return _centered(create_light(light_type, pos, module, first_light_id))
