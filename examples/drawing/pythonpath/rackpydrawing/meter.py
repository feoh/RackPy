"""Meter: a voltmeter whose panel is drawn by custom widgets.

Generated from res/Meter.svg by bin/rackpy-plugin add-module (its yellow rectangles became
the four widget classes), then edited. Each widget shows one way to draw in Rack:

- Shapes: NanoVG paths, filled and stroked (rackpy.raw.nanovg).
- Readout: text, with the font loaded from Rack's window every frame.
- Bar: Rack's self-illuminating layer, which room brightness does not dim
  (the rackpy.drawing facade).
- Scale: a FramebufferWidget, which Rack renders once and redraws from its image
  until it is marked dirty, here when RANGE changes.

The widgets run on Rack's UI thread and read the input's voltage there, as C++
widgets do. Rack's engine passes SIGNAL to THROUGH as a native kernel. In the
module browser the widget has no module, so each widget draws a resting meter.
"""

from enum import IntEnum

from rackpy import assets, raw
from rackpy.drawing import Painter
from rackpy.dsp.graph import Graph
from rackpy.engine import Module
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw import nanovg as nvg
from rackpy.raw import widget as w
from rackpy.raw.math import Vec
from rackpy.raw.window import mm2px


class Param(IntEnum):
    RANGE = 0


class Input(IntEnum):
    SIGNAL = 0


class Output(IntEnum):
    THROUGH = 0


RANGES = (5.0, 10.0)  # volts at either end of the scale, by RANGE's position
FONT = "res/fonts/ShareTechMono-Regular.ttf"  # Rack's own font, in Rack's system folder
"""The readout's font. Rack ships it (SIL Open Font License); this plugin does not copy it."""


def voltage(module: Module | None) -> float:
    """The input's voltage now, or 0 without a module (the browser's preview)."""
    return module.inputs[Input.SIGNAL].get_voltage() if module is not None and module.alive else 0.0


def full_scale(module: Module | None) -> float:
    return RANGES[round(module.params[Param.RANGE].value)] if module is not None and module.alive else RANGES[0]


class Shapes(w.TransparentWidget):
    """Shapes drawn with NanoVG: a rounded panel, a filled circle, and a stroked triangle."""

    def draw(self, args: w.DrawArgs) -> None:
        vg = args.vg  # valid only during this call
        assert vg is not None  # Rack always passes a context to draw()
        width, height = self.box.size.x, self.box.size.y
        # A rounded rectangle, filled dark blue-grey.
        nvg.begin_path(vg)
        nvg.rounded_rect(vg, 0, 0, width, height, 4)
        nvg.fill_color(vg, nvg.rgb(0x2E, 0x36, 0x44))
        nvg.fill(vg)
        # A circle, filled amber.
        nvg.begin_path(vg)
        nvg.circle(vg, width * 0.25, height / 2, height * 0.3)
        nvg.fill_color(vg, nvg.rgb(0xF2, 0xA6, 0x3B))
        nvg.fill(vg)
        # A triangle with a red stroke and no fill.
        nvg.begin_path(vg)
        nvg.move_to(vg, width * 0.55, height * 0.8)
        nvg.line_to(vg, width * 0.7, height * 0.2)
        nvg.line_to(vg, width * 0.85, height * 0.8)
        nvg.close_path(vg)
        nvg.stroke_color(vg, nvg.rgb(0xE0, 0x30, 0x30))
        nvg.stroke_width(vg, 2.0)
        nvg.stroke(vg)


class Readout(w.TransparentWidget):
    """The input's voltage as text."""

    module: Module | None = None

    def draw(self, args: w.DrawArgs) -> None:
        context = raw.context_get()
        if context is None or context.window is None:
            return
        # Load the font every frame: Rack caches it, and a font kept across frames goes stale
        # when Rack replaces its graphics context. None means Rack could not load it (it logs
        # why), so draw nothing rather than fail every frame.
        font = context.window.load_font(str(assets.system(FONT)))
        if font is None:
            return
        vg = args.vg  # valid only during this call
        assert vg is not None  # Rack always passes a context to draw()
        nvg.font_face_id(vg, font.handle)
        nvg.font_size(vg, 26)
        nvg.fill_color(vg, nvg.rgb(0x8F, 0xD0, 0xFF))
        nvg.text_align(vg, nvg.NVG_ALIGN_RIGHT | nvg.NVG_ALIGN_MIDDLE)
        nvg.text(vg, self.box.size.x - 6, self.box.size.y / 2, f"{voltage(self.module):+.2f} V")


class Bar(w.TransparentWidget):
    """A bar from the centre towards the voltage, on Rack's self-lit layer: it glows in a dark room."""

    module: Module | None = None

    def draw_layer(self, args: w.DrawArgs, layer: int) -> None:
        if layer == 1:  # Rack draws layer 1 after dimming the rack by the room brightness
            width, height = self.box.size.x, self.box.size.y
            share = max(-1.0, min(1.0, voltage(self.module) / full_scale(self.module)))
            left = width / 2 + min(share, 0.0) * width / 2
            Painter(args).rect(left, height * 0.6, abs(share) * width / 2, height * 0.3).fill("#6CF29BFF")
        super().draw_layer(args, layer)


class Ticks(w.Widget):
    """The scale's tick marks, one per volt; drawn only when the framebuffer above renders."""

    module: Module | None = None

    def draw(self, args: w.DrawArgs) -> None:
        vg = args.vg  # valid only during this call
        assert vg is not None  # Rack always passes a context to draw()
        width, height = self.box.size.x, self.box.size.y
        volts = int(full_scale(self.module))
        nvg.begin_path(vg)
        for v in range(-volts, volts + 1):
            x = width / 2 + v / volts * (width / 2 - 1)
            long = v % 5 == 0
            nvg.move_to(vg, x, height * 0.55)
            nvg.line_to(vg, x, height * (0.1 if long else 0.35))
        nvg.stroke_color(vg, nvg.rgb(0xC8, 0xD0, 0xDC))
        nvg.stroke_width(vg, 1.0)
        nvg.stroke(vg)


class Scale(w.FramebufferWidget):
    """Caches Ticks: Rack renders it once into an image and draws the image every frame after."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.module = module
        self.ticks = Ticks()
        self.ticks.module = module
        self.add_child(self.ticks)
        self.shown = full_scale(module)

    def step(self) -> None:
        # The ticks depend only on RANGE: mark the cache dirty when it changes, so Rack renders again.
        if full_scale(self.module) != self.shown:
            self.shown = full_scale(self.module)
            self.set_dirty()
        super().step()


GRAPH = Graph()
GRAPH.output(Output.THROUGH, GRAPH.input(Input.SIGNAL))


def configure(module: Module) -> None:
    """Rack calls this to build each module: configure its controls here."""
    module.config(len(Param), len(Input), len(Output))
    module.config_switch(Param.RANGE, 0.0, 1.0, 0.0, "Range", ["5 V", "10 V"])
    module.config_input(Input.SIGNAL, "Signal")
    module.config_output(Output.THROUGH, "Signal")
    module.config_bypass(Input.SIGNAL, Output.THROUGH)
    module.set_kernel(GRAPH.compile())


class MeterWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/Meter.svg"))))
        grid, right = app.RACK_GRID_WIDTH, self.box.size.x - 2 * app.RACK_GRID_WIDTH
        bottom = app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        for x, y in ((grid, 0), (right, 0), (grid, bottom), (right, bottom)):
            self.add_child(raw.create_widget(cl.ScrewSilver, Vec(x, y)))
        self.add_param(raw.create_param_centered(cl.CKSS, mm2px(Vec(30.48, 112.0)), module, Param.RANGE))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(12.0, 112.0)), module, Input.SIGNAL))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(48.96, 112.0)), module, Output.THROUGH))
        # A custom widget: create it, give it a position and size, and add it to the panel.
        self.shapes = raw.create_widget(Shapes, mm2px(Vec(5.0, 18.0)))
        self.shapes.box.size = mm2px(Vec(50.96, 30.0))
        self.add_child(self.shapes)
        self.readout = raw.create_widget(Readout, mm2px(Vec(5.0, 52.0)))
        self.readout.box.size = mm2px(Vec(50.96, 12.0))
        self.readout.module = module
        self.add_child(self.readout)
        self.scale = Scale(module)
        self.scale.box.pos = mm2px(Vec(5.0, 68.0))
        self.scale.box.size = self.scale.ticks.box.size = mm2px(Vec(50.96, 30.0))
        self.add_child(self.scale)
        self.bar = raw.create_widget(Bar, mm2px(Vec(5.0, 68.0)))
        self.bar.box.size = mm2px(Vec(50.96, 30.0))
        self.bar.module = module
        self.add_child(self.bar)
