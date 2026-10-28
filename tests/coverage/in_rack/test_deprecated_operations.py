# SPDX-License-Identifier: GPL-3.0-or-later
"""Warning checks keyed to individual pinned SDK declarations, in Rack."""

import json
import warnings
from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest

from rackpy import laboratory, raw
from rackpy.raw import app, componentlibrary, dsp, engine, widget


def trigger_state(cls: Any) -> Any:
    trigger = cls()
    trigger.process(False if cls is dsp.BooleanTrigger else 0.0)
    return trigger.state


def legacy_module_event(name: str) -> None:
    module = engine.Module.create(lambda m: m.config(0, 0, 0, 0))
    try:
        getattr(module, name)()
    finally:
        module.close()


def legacy_event_state(name: str) -> None:
    context = raw.context_get()
    assert context is not None and context.event is not None
    method = getattr(context.event, name)
    method(None, 0) if name == "set_dragged" else method(None)


def legacy_svg(cls: Any, method: str, count: int = 1) -> None:
    svg = raw.window.Svg()
    svg.load_string('<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10"/>')
    instance = cls()
    try:
        getattr(instance, method)(*[svg] * count)
    finally:
        instance.close()


def legacy_slider_svgs() -> None:
    legacy_svg(app.SvgSlider, "set_svgs", 2)


def deprecated_module_constructor() -> None:
    engine.Module.create(0, 0, 0, 0).close()


def deprecated_module_widget_constructor() -> None:
    app.ModuleWidget(None).close()


def deprecated_param_handle() -> object:
    module = engine.Module.create()
    try:
        context = raw.context_get()
        assert context is not None and context.engine is not None
        return context.engine.get_param_handle(module, 0)
    finally:
        module.close()


def scene() -> app.HostScene:
    context = raw.context_get()
    assert context is not None and context.scene is not None
    return context.scene


def incomplete_cable() -> object:
    return scene().rack.get_incomplete_cable()


def legacy_widget_draw() -> object:
    return type("LegacyDraw", (widget.Widget,), {"draw_vg": lambda self, vg: None})


def legacy_module_step() -> object:
    return type("LegacyStep", (laboratory.ModuleClass,), {"step": lambda self: None})


# Each operation selects the deprecated overload, not a modern overload with the
# same Python name. Cases remain explicit so the SDK ID cannot silently point
# to an unrelated call.
CASES: dict[str, Callable[[], object]] = {
    "alias_template:rack::componentlibrary::LEDLightButton<type>": lambda: componentlibrary.LEDLightButton[
        componentlibrary.RedLight
    ],
    "constructor:rack::app::ModuleWidget::ModuleWidget(rack::engine::Module *)": deprecated_module_widget_constructor,
    "constructor:rack::engine::Module::Module(int, int, int, int)": deprecated_module_constructor,
    "field:rack::app::RackWidget::touchedParam": lambda: scene().rack.touched_param,
    "field:rack::app::Scene::mousePos": lambda: scene().mouse_pos,
    "field:rack::dsp::BooleanTrigger::state": lambda: trigger_state(dsp.BooleanTrigger),
    "field:rack::dsp::TSchmittTrigger<float>::state": lambda: trigger_state(dsp.SchmittTrigger),
    "field:rack::engine::Port::active": lambda: engine.Port().active,
    "field:rack::engine::Port::channels": lambda: engine.Port().channels,
    "field:rack::engine::Port::value": lambda: engine.Port().value,
    "function:rack::appGet()": raw.app_get,
    "function_template:rack::dsp::approxExp2Floor<type>($0, $0 *)": lambda: dsp.approx_exp2_floor(3.25),
    "function_template:rack::dsp::approxExp2_taylor5<type>($0)": lambda: dsp.approx_exp2_taylor5(3.0),
    "method:rack::app::RackWidget::getIncompleteCable()": incomplete_cable,
    "method:rack::app::SvgKnob::setSVG(std::shared_ptr<rack::window::Svg>)": lambda: legacy_svg(
        app.SvgKnob, "set_svg_legacy"
    ),
    "method:rack::app::SvgPort::setSVG(std::shared_ptr<rack::window::Svg>)": lambda: legacy_svg(
        app.SvgPort, "set_svg_legacy"
    ),
    "method:rack::app::SvgSlider::setBackgroundSVG(std::shared_ptr<rack::window::Svg>)": lambda: legacy_svg(
        app.SvgSlider, "set_background_svg_legacy"
    ),
    "method:rack::app::SvgSlider::setHandleSVG(std::shared_ptr<rack::window::Svg>)": lambda: legacy_svg(
        app.SvgSlider, "set_handle_svg_legacy"
    ),
    (
        "method:rack::app::SvgSlider::setSVGs(std::shared_ptr<rack::window::Svg>, std::shared_ptr<rack::window::Svg>)"
    ): legacy_slider_svgs,
    "method:rack::dsp::TExponentialFilter::process($c0)": lambda: dsp.ExponentialFilter().process(1.0),
    "method:rack::dsp::TExponentialSlewLimiter::process($c0)": lambda: dsp.ExponentialSlewLimiter().process(1.0),
    "method:rack::dsp::TPeakFilter::peak()": lambda: dsp.PeakFilter().peak(),
    "method:rack::dsp::TPeakFilter::process($c0)": lambda: dsp.PeakFilter().process(1.0),
    "method:rack::dsp::TPeakFilter::setRate($c0)": lambda: dsp.PeakFilter().set_rate(0.5),
    "method:rack::dsp::TSlewLimiter::process($c0)": lambda: dsp.SlewLimiter().process(1.0),
    "method:rack::engine::Engine::getParamHandle(rack::engine::Module *, int)": deprecated_param_handle,
    "method:rack::engine::Light::setBrightnessSmooth(float, int)": lambda: engine.Light().set_brightness_smooth(1, 2),
    "method:rack::engine::Light::setSmoothBrightness(float, float)": lambda: engine.Light().set_smooth_brightness(
        1, 0.1
    ),
    "method:rack::engine::Module::onAdd()": lambda: legacy_module_event("on_add"),
    "method:rack::engine::Module::onRemove()": lambda: legacy_module_event("on_remove"),
    "method:rack::engine::Module::onReset()": lambda: legacy_module_event("on_reset"),
    "method:rack::engine::Module::onRandomize()": lambda: legacy_module_event("on_randomize"),
    "method:rack::engine::Module::onSampleRateChange()": lambda: legacy_module_event("on_sample_rate_change"),
    "method:rack::engine::Module::step()": legacy_module_step,
    "method:rack::engine::ParamQuantity::getSmoothValue()": lambda: engine.ParamQuantity().get_smooth_value(),
    "method:rack::engine::ParamQuantity::setSmoothValue(float)": lambda: engine.ParamQuantity().set_smooth_value(0.5),
    "method:rack::engine::Port::normalize(float)": lambda: engine.Port().normalize(1.0),
    "method:rack::widget::EventState::setDragHovered(rack::widget::Widget *)": lambda: legacy_event_state(
        "set_drag_hovered"
    ),
    "method:rack::widget::EventState::setDragged(rack::widget::Widget *, int)": lambda: legacy_event_state(
        "set_dragged"
    ),
    "method:rack::widget::EventState::setHovered(rack::widget::Widget *)": lambda: legacy_event_state("set_hovered"),
    "method:rack::widget::EventState::setSelected(rack::widget::Widget *)": lambda: legacy_event_state("set_selected"),
    "method:rack::widget::SvgWidget::setSVG(std::shared_ptr<rack::window::Svg>)": lambda: legacy_svg(
        widget.SvgWidget, "set_svg_legacy"
    ),
    "method:rack::widget::Widget::draw(NVGcontext *)": legacy_widget_draw,
    "method:rack::window::Font::load(const std::string &)": lambda: raw.window.Font.load(
        raw.asset.system("res/fonts/DejaVuSans.ttf")
    ),
    "method:rack::window::Image::load(const std::string &)": lambda: raw.window.Image.load(
        raw.asset.system("res/icon.png")
    ),
    "record:rack::componentlibrary::LEDSliderGreen": lambda: componentlibrary.LEDSliderGreen().close(),
    "record:rack::dsp::VuMeter": dsp.VuMeter,
}


def declared_cases() -> list[str]:
    repo = Path(__file__).resolve().parents[3]
    entries = json.loads((repo / "upstream/api-surface.json").read_text())["entries"]
    records = json.loads((repo / "bindings/manifest.json").read_text())["entries"]
    recorded = {entry["id"] for entry in records}
    expected = {
        entry["id"]
        for entry in entries
        if entry["kind"] != "alias"
        and (entry.get("deprecated") or entry.get("comment_deprecated"))
        and entry["id"] in recorded
    }
    assert CASES.keys() == expected, f"warning cases differ from inventory: {CASES.keys() ^ expected}"
    return sorted(CASES)


@pytest.mark.parametrize("entry_id", declared_cases())
def test_deprecated_operation_warns(entry_id: str) -> None:
    with pytest.warns(DeprecationWarning):
        CASES[entry_id]()


@pytest.mark.parametrize(
    ("name", "event"),
    [
        ("on_add", engine.Module.AddEvent),
        ("on_remove", engine.Module.RemoveEvent),
        ("on_reset", engine.Module.ResetEvent),
        ("on_randomize", engine.Module.RandomizeEvent),
        ("on_sample_rate_change", engine.Module.SampleRateChangeEvent),
    ],
)
def test_modern_module_event_overloads_do_not_warn(name: str, event: Any) -> None:
    module = engine.Module.create(lambda m: m.config(0, 0, 0, 0))
    try:
        with warnings.catch_warnings(record=True) as seen:
            warnings.simplefilter("always", DeprecationWarning)
            getattr(module, name)(event())
        assert not [warning for warning in seen if issubclass(warning.category, DeprecationWarning)]
    finally:
        module.close()
