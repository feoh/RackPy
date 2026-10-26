# SPDX-License-Identifier: GPL-3.0-or-later
"""rack::componentlibrary's class templates, as Python template classes (RP-28C).

Rack's component library composes its lights, sliders and buttons with CRTP
templates: ``MediumLight<RedLight>``, ``LightButton<VCVButton, GreenLight>``,
``RectangleLight<TBase>``. Each template's body is header code: a
constructor that sets fields or adds children, and sometimes a drawing
override. Here each template is a class holding that code, and
``MediumLight[RedLight]`` is its specialization: a cached class deriving from
the template class and from the base, so it runs the same code. A template
class itself is not instantiable, as in C++. ``isinstance(x, MediumLight)``
holds for every specialization. Rack's aliases (``RedLight`` is
``TRedLight<>``) are the default specializations, and its alias templates
(``VCVLightButton<TLight>`` is ``LightButton<VCVButton, TLight>``) give the
same classes. The library's concrete classes (RoundBlackKnob, PJ301MPort,
CKSS, ScrewSilver, ...) are Rack's own C++ classes, which load Rack's
installed artwork.

This module adds the templates to the native ``rackpy.raw.componentlibrary``.
"""

from collections.abc import Callable
from typing import Any, ClassVar

import _rackpy_host

from rackpy.raw import app as _app
from rackpy.raw import asset as _asset
from rackpy.raw import color as _color
from rackpy.raw import nanovg as _nvg
from rackpy.raw import widget as _widget
from rackpy.raw.math import Vec as _Vec
from rackpy.raw.window import Svg as _Svg
from rackpy.raw.window import mm2px as _mm2px

_lib: Any = _rackpy_host.raw.componentlibrary
_Params = tuple[Callable[[], type] | None, ...]
_MODULE = "rackpy.raw.componentlibrary"


def _system_svg(name: str) -> _Svg | None:
    return _Svg.load(_asset.system(f"res/ComponentLibrary/{name}"))


class Template:
    """A C++ class template. ``Template[A]`` or ``Template[A, B]`` is its specialization, a cached class.

    Subclasses hold the template's code and say how a specialization is built:
    ``_params`` gives each template argument's default (None: required), and
    ``_bases(*args)`` the specialization's base classes after the template class.
    """

    _params: ClassVar[_Params] = ()
    _specializations: ClassVar[dict[tuple[type, ...], type]]
    _args: ClassVar[tuple[type, ...]] = ()

    def __init_subclass__(cls, **kwargs: Any) -> None:
        super().__init_subclass__(**kwargs)
        if "_params" in vars(cls):  # a template, not a specialization
            cls._specializations = {}

    def __new__(cls, *args: Any, **kwargs: Any) -> Any:
        if "_params" in vars(cls):
            raise TypeError(f"{cls.__name__} is a class template: instantiate a specialization, {cls.__name__}[...]")
        return super().__new__(cls, *args, **kwargs)

    @classmethod
    def _bases(cls, *args: type) -> tuple[type, ...]:
        return (args[0],)

    def __class_getitem__(cls, args: Any) -> type:
        given = args if isinstance(args, tuple) else (args,)
        params = cls._params
        if len(given) > len(params):
            raise TypeError(f"{cls.__name__} takes {len(params)} template argument(s)")
        full = list(given)
        for default in params[len(given) :]:
            if default is None:
                raise TypeError(f"{cls.__name__} needs {len(params)} template argument(s)")
            full.append(default())
        for arg in full:
            if not isinstance(arg, type):
                raise TypeError(f"{cls.__name__}'s template arguments are classes, not {arg!r}")
        key = tuple(full)
        spec = cls._specializations.get(key)
        if spec is None:
            name = f"{cls.__name__}[{', '.join(a.__name__ for a in key)}]"
            # Each template (and template it derives from) reads its own arguments, so a specialization
            # deriving from another (VCVLightLatch[L] from LightButton[VCVButton, L]) keeps the base's.
            attrs: dict[str, Any] = {"__module__": cls.__module__, "__qualname__": name, "_args": key}
            for ancestor in cls.__mro__:
                if isinstance(ancestor, type) and issubclass(ancestor, Template) and "_params" in vars(ancestor):
                    attrs[f"_args_{ancestor.__name__}"] = key
            spec = type(name, (cls, *cls._bases(*key)), attrs)
            cls._specializations[key] = spec
        return spec

    @classmethod
    def default(cls) -> type:
        """The specialization with Rack's default arguments (``Template<>``)."""
        return cls.__class_getitem__(())


def _template_class(cls: type) -> type:
    cls.__module__ = _MODULE
    return cls


class AliasTemplate:
    """A C++ alias template: ``Alias[A]`` is the aliased specialization itself."""

    def __init__(self, name: str, target: Callable[..., type], defaults: tuple[Callable[[], type] | None, ...]) -> None:
        self.__name__ = name
        self._target = target
        self._defaults = defaults

    def __getitem__(self, args: Any) -> type:
        given = args if isinstance(args, tuple) else (args,)
        full = list(given)
        for default in self._defaults[len(given) :]:
            if default is None:
                raise TypeError(f"{self.__name__} needs {len(self._defaults)} template argument(s)")
            full.append(default())
        return self._target(*full)

    def __repr__(self) -> str:
        return f"<alias template {_MODULE}.{self.__name__}>"


def _gray() -> type:
    gray: type = _lib.GrayModuleLightWidget
    return gray


def _light(name: str) -> Callable[[], type]:
    def get() -> type:
        cls: type = getattr(_lib, name)
        return cls

    return get


# Lights


@_template_class
class TSvgLight(Template):
    _params: ClassVar[_Params] = (lambda: _app.ModuleLightWidget,)
    fb: _widget.FramebufferWidget
    sw: _widget.SvgWidget

    def __init__(self) -> None:
        super().__init__()
        self.fb = _widget.FramebufferWidget()
        self.add_child(self.fb)  # type: ignore[attr-defined]
        self.sw = _widget.SvgWidget()
        self.fb.add_child(self.sw)

    def set_svg(self, svg: _Svg | None) -> None:
        self.sw.set_svg(svg)
        self.fb.box.size = self.sw.box.size
        self.box.size = self.sw.box.size  # type: ignore[attr-defined]


@_template_class
class TGrayModuleLightWidget(Template):
    _params: ClassVar[_Params] = (lambda: _app.ModuleLightWidget,)

    def __init__(self) -> None:
        super().__init__()
        self.bg_color = _nvg.rgba(0x33, 0x33, 0x33, 0xFF)
        self.border_color = _nvg.rgba(0, 0, 0, 53)


class _ColorLight(Template):
    _colors: ClassVar[tuple[str, ...]] = ()

    def __init__(self) -> None:
        super().__init__()
        for color in self._colors:
            self.add_base_color(getattr(_lib, color))  # type: ignore[attr-defined]


def _color_light(name: str, *colors: str) -> type:
    return _template_class(type(name, (_ColorLight,), {"_params": (_gray,), "_colors": colors, "__qualname__": name}))


TWhiteLight = _color_light("TWhiteLight", "SCHEME_WHITE")
TRedLight = _color_light("TRedLight", "SCHEME_RED")
TGreenLight = _color_light("TGreenLight", "SCHEME_GREEN")
TBlueLight = _color_light("TBlueLight", "SCHEME_BLUE")
TYellowLight = _color_light("TYellowLight", "SCHEME_YELLOW")
TGreenRedLight = _color_light("TGreenRedLight", "SCHEME_GREEN", "SCHEME_RED")
TRedGreenBlueLight = _color_light("TRedGreenBlueLight", "SCHEME_RED", "SCHEME_GREEN", "SCHEME_BLUE")


class _SvgFileLight(TSvgLight):
    _svg: ClassVar[str] = ""

    def __init__(self) -> None:
        super().__init__()
        self.set_svg(_system_svg(self._svg))


def _svg_light(name: str, svg: str) -> type:
    return _template_class(type(name, (_SvgFileLight,), {"_params": (None,), "_svg": svg, "__qualname__": name}))


LargeLight = _svg_light("LargeLight", "LargeLight.svg")
MediumLight = _svg_light("MediumLight", "MediumLight.svg")
SmallLight = _svg_light("SmallLight", "SmallLight.svg")
TinyLight = _svg_light("TinyLight", "TinyLight.svg")


class _SimpleLight(Template):
    _mm: ClassVar[float] = 0

    def __init__(self) -> None:
        super().__init__()
        self.box.size = _mm2px(_Vec(self._mm, self._mm))  # type: ignore[attr-defined]


def _simple_light(name: str, mm: float) -> type:
    return _template_class(type(name, (_SimpleLight,), {"_params": (_gray,), "_mm": mm, "__qualname__": name}))


LargeSimpleLight = _simple_light("LargeSimpleLight", 5)
MediumSimpleLight = _simple_light("MediumSimpleLight", 3)
SmallSimpleLight = _simple_light("SmallSimpleLight", 2)
TinySimpleLight = _simple_light("TinySimpleLight", 1)


@_template_class
class RectangleLight(Template):
    _params: ClassVar[_Params] = (None,)

    def draw_background(self, args: Any) -> None:
        # Rack's: LightWidget::drawBackground with a rectangle.
        me: Any = self
        vg = args.vg
        _nvg.begin_path(vg)
        _nvg.rect(vg, 0, 0, me.box.size.x, me.box.size.y)
        if me.bg_color.a > 0.0:
            _nvg.fill_color(vg, me.bg_color)
            _nvg.fill(vg)
        if me.border_color.a > 0.0:
            _nvg.stroke_width(vg, 0.5)
            _nvg.stroke_color(vg, me.border_color)
            _nvg.stroke(vg)

    def draw_light(self, args: Any) -> None:
        # Rack's: LightWidget::drawLight with a rectangle.
        me: Any = self
        if me.color.a > 0.0:
            vg = args.vg
            _nvg.begin_path(vg)
            _nvg.rect(vg, 0, 0, me.box.size.x, me.box.size.y)
            _nvg.fill_color(vg, me.color)
            _nvg.fill(vg)


@_template_class
class VCVBezelLight(Template):
    _params: ClassVar[_Params] = (None,)

    def __init__(self) -> None:
        super().__init__()
        me: Any = self
        me.border_color = _color.BLACK_TRANSPARENT
        me.bg_color = _color.BLACK_TRANSPARENT
        me.box.size = _Vec(17.545, 17.545)


@_template_class
class PB61303Light(Template):
    _params: ClassVar[_Params] = (None,)

    def __init__(self) -> None:
        super().__init__()
        me: Any = self
        me.bg_color = _color.BLACK_TRANSPARENT
        me.box.size = _mm2px(_Vec(9.0, 9.0))


# Sliders


@_template_class
class LightSlider(Template):
    """``LightSlider<TBase, TLightBase>``: a slider with a light that follows its handle."""

    _params: ClassVar[_Params] = (None, _light("RedLight"))
    light: Any

    @classmethod
    def _light_class(cls) -> type:
        light: type = cls._args_LightSlider[1]  # type: ignore[attr-defined]
        return light

    def __init__(self) -> None:
        super().__init__()
        self.light = type(self)._light_class()()
        self.add_child(self.light)  # type: ignore[attr-defined]

    def get_light(self) -> Any:
        return self.light

    def step(self) -> None:
        me: Any = self
        super().step()  # type: ignore[misc]
        handle = me.handle.get_box()
        self.light.box.pos = handle.pos.plus(handle.size.div(2)).minus(self.light.box.size.div(2))


@_template_class
class VCVSliderLight(RectangleLight, TSvgLight):
    """``VCVSliderLight<TBase>`` is ``RectangleLight<TSvgLight<TBase>>`` with Rack's slider light SVG."""

    _params: ClassVar[_Params] = (None,)

    def __init__(self) -> None:
        super().__init__()
        self.set_svg(_system_svg("VCVSliderLight.svg"))


@_template_class
class VCVLightSlider(LightSlider):
    """``VCVLightSlider<TLightBase>`` is ``LightSlider<VCVSlider, VCVSliderLight<TLightBase>>``."""

    _params: ClassVar[_Params] = (_light("RedLight"),)

    @classmethod
    def _bases(cls, *args: type) -> tuple[type, ...]:
        return (_lib.VCVSlider,)

    @classmethod
    def _light_class(cls) -> type:
        light: type = VCVSliderLight.__class_getitem__(cls._args_VCVLightSlider[0])  # type: ignore[attr-defined]
        return light


# Buttons and switches


@_template_class
class MomentarySwitch(Template):
    _params: ClassVar[_Params] = (None,)

    def __init__(self) -> None:
        super().__init__()
        self.momentary = True


@_template_class
class LightButton(Template):
    """``LightButton<TBase, TLight>``: a button with a light at its centre."""

    _params: ClassVar[_Params] = (None, _light("WhiteLight"))
    light: Any

    def __init__(self) -> None:
        super().__init__()
        me: Any = self
        self.light = type(self)._args_LightButton[1]()  # type: ignore[attr-defined]
        self.light.box.pos = me.box.size.div(2).minus(self.light.box.size.div(2))
        me.add_child(self.light)

    def get_light(self) -> Any:
        return self.light


@_template_class
class VCVLightLatch(Template):
    """``VCVLightLatch<TLight>`` derives from ``VCVLightButton<TLight>`` and latches."""

    _params: ClassVar[_Params] = (None,)

    @classmethod
    def _bases(cls, *args: type) -> tuple[type, ...]:
        return (LightButton[_lib.VCVButton, args[0]],)  # type: ignore[misc]

    def __init__(self) -> None:
        super().__init__()
        me: Any = self
        me.momentary = False
        me.latch = True


@_template_class
class VCVLightBezel(Template):
    """``VCVLightBezel<TLightBase>``: Rack's bezel button with a VCVBezelLight<TLightBase> at its centre."""

    _params: ClassVar[_Params] = (_light("WhiteLight"),)
    light: Any

    @classmethod
    def _bases(cls, *args: type) -> tuple[type, ...]:
        return (_lib.VCVBezel,)

    def __init__(self) -> None:
        super().__init__()
        me: Any = self
        self.light = VCVBezelLight.__class_getitem__(type(self)._args_VCVLightBezel[0])()  # type: ignore[attr-defined]
        self.light.box.pos = me.box.size.div(2).minus(self.light.box.size.div(2))
        me.add_child(self.light)

    def get_light(self) -> Any:
        return self.light


@_template_class
class VCVLightBezelLatch(VCVLightBezel):
    _params: ClassVar[_Params] = (_light("WhiteLight"),)

    def __init__(self) -> None:
        super().__init__()
        me: Any = self
        me.momentary = False
        me.latch = True


def _set_lights(self: Any, module: Any, first_light_id: int, lights_len: int, light_base: type | None = None) -> None:
    """Rack's SegmentDisplay::setLights<TLightBase>: lights_len RectangleLight[light_base] segments."""
    base = _lib.WhiteLight if light_base is None else light_base
    self.clear_children()
    self.lights_len = lights_len
    r = (self.box.size.y if self.vertical else self.box.size.x) - self.margin
    for i in range(lights_len):
        p = i / lights_len
        light = RectangleLight[base]()  # type: ignore[misc]
        if self.vertical:
            light.box.pos.y = p * r + self.margin
            light.box.size.y = r / lights_len - self.margin
            light.box.size.x = self.box.size.x
        else:
            light.box.pos.x = p * r + self.margin
            light.box.size.x = r / lights_len - self.margin
            light.box.size.y = self.box.size.y
        light.first_light_id = first_light_id
        light.module = module
        first_light_id += len(light.base_colors)
        self.add_child(light)


def _install() -> None:
    templates: dict[str, Any] = {
        cls.__name__: cls
        for cls in (
            TSvgLight, TGrayModuleLightWidget, TWhiteLight, TRedLight, TGreenLight, TBlueLight, TYellowLight,
            TGreenRedLight, TRedGreenBlueLight, LargeLight, MediumLight, SmallLight, TinyLight, LargeSimpleLight,
            MediumSimpleLight, SmallSimpleLight, TinySimpleLight, RectangleLight, VCVBezelLight, PB61303Light,
            LightSlider, VCVSliderLight, VCVLightSlider, MomentarySwitch, LightButton, VCVLightLatch, VCVLightBezel,
            VCVLightBezelLatch,
        )
    }  # fmt: skip
    for name, cls in templates.items():
        setattr(_lib, name, cls)
    _lib.Template = Template
    # Rack's alias templates: the aliased specializations themselves.
    _lib.LEDBezelLight = AliasTemplate("LEDBezelLight", lambda base: VCVBezelLight[base], (None,))  # type: ignore[misc]
    _lib.LEDSliderLight = AliasTemplate("LEDSliderLight", lambda base: VCVSliderLight[base], (None,))  # type: ignore[misc]
    _lib.LEDLightSlider = AliasTemplate("LEDLightSlider", lambda light: VCVLightSlider[light], (_light("RedLight"),))  # type: ignore[misc]
    _lib.VCVLightButton = AliasTemplate(
        "VCVLightButton",
        lambda light: LightButton[_lib.VCVButton, light],  # type: ignore[misc]
        (_light("WhiteLight"),),
    )
    _lib.LEDLightButton = AliasTemplate("LEDLightButton", lambda light: _lib.VCVLightButton[light], (None,))
    _lib.LEDLightBezel = AliasTemplate("LEDLightBezel", lambda light: VCVLightBezel[light], (_light("WhiteLight"),))  # type: ignore[misc]
    # Rack's aliases for default specializations (``using RedLight = TRedLight<>``), in dependency order.
    _lib.SvgLight = TSvgLight.default()
    _lib.GrayModuleLightWidget = TGrayModuleLightWidget.default()
    for color in ("White", "Red", "Green", "Blue", "Yellow", "GreenRed", "RedGreenBlue"):
        setattr(_lib, f"{color}Light", templates[f"T{color}Light"].default())
    for alias in (
        "SvgLight",
        "GrayModuleLightWidget",
        *(f"{c}Light" for c in ("White", "Red", "Green", "Blue", "Yellow", "GreenRed", "RedGreenBlue")),
    ):
        cls = getattr(_lib, alias)
        cls.__name__ = cls.__qualname__ = alias  # as C++ spells them; the cache keeps one class per specialization
    # Rack's (deprecated) concrete light sliders: struct LEDSliderGreen : VCVLightSlider<GreenLight> {}.
    for color in ("Green", "Red", "Yellow", "Blue", "White"):
        name = f"LEDSlider{color}"
        parent = VCVLightSlider.__class_getitem__(getattr(_lib, f"{color}Light"))
        setattr(_lib, name, type(name, (parent,), {"__module__": _MODULE, "__qualname__": name}))
    _lib.SegmentDisplay.set_lights = _set_lights


_install()
