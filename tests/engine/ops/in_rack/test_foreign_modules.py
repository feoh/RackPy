# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-24D: other plugins' models from Python, in Rack's live UI: Core's Notes and Blank."""

import gc

import pytest

from rackpy import raw
from rackpy.errors import OwnershipError, StaleHandleError
from rackpy.raw import app
from rackpy.raw import history as h
from rackpy.raw.engine import Engine, EngineModule


def engine() -> Engine:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    return context.engine


def rack() -> app.HostRackWidget:
    context = raw.context_get()
    assert context is not None and context.scene is not None
    return context.scene.rack


def in_engine(module: EngineModule) -> bool:
    return module.in_engine  # calls, so that type checkers do not narrow them between engine calls


def alive(module: EngineModule) -> bool:
    return module.alive


def widget_state(widget: app.ForeignModuleWidget) -> tuple[bool, bool]:
    return widget.added, widget.owned


def core(slug: str) -> raw.plugin.Model:
    model = raw.plugin.get_model("Core", slug)
    assert model is not None
    return model


def test_another_plugins_module_into_the_engine_and_rack() -> None:
    e, notes = engine(), core("Notes")
    module = notes.create_module()
    assert isinstance(module, EngineModule) and not in_engine(module) and alive(module)
    assert module.model is notes and module.id == -1
    with pytest.raises(OwnershipError, match="must be in Rack's engine first"):
        notes.create_module_widget(module)  # its widget would remove it from the engine when deleted
    with pytest.raises(OwnershipError, match="does not have"):
        e.remove_module(module)
    e.add_module(module)
    assert in_engine(module) and module.id >= 0 and e.get_module(module.id) == module
    with pytest.raises(OwnershipError, match="already has"):
        e.add_module(module)
    with pytest.raises(ValueError, match="not of this model"):
        core("Blank").create_module_widget(module)

    widget = notes.create_module_widget(module)
    assert isinstance(widget, app.ForeignModuleWidget) and widget.owned and widget.model is notes
    assert widget.module == module and widget.box.size.y == 380
    with pytest.raises(OwnershipError, match="already owns"):
        notes.create_module_widget(module)
    with pytest.raises(OwnershipError, match="widget owns it"):
        e.remove_module(module)
    rack().add_module_at_mouse(widget)
    assert widget_state(widget) == (True, False) and rack().get_module(module.id) is not None
    with pytest.raises(StaleHandleError, match="rack has"):
        _ = widget.box

    # Rack's own action deletes the widget with its module, and recreates both.
    removal = h.ModuleRemove()
    view = rack().get_module(module.id)
    assert view is not None
    removal.set_module(view)
    module_id = module.id
    removal.redo()
    assert not alive(module) and rack().get_module(module_id) is None
    removal.undo()
    assert alive(module) and rack().get_module(module_id) is not None
    removal.redo()  # leave the rack as it was
    assert not alive(module)


def test_previews_and_dropped_handles() -> None:
    e, blank = engine(), core("Blank")
    preview = blank.create_module_widget()
    assert isinstance(preview, app.ForeignModuleWidget) and preview.module is None
    with pytest.raises(OwnershipError, match="preview"):
        rack().add_module(preview)  # Rack indexes the rack's widgets by their module
    preview.close()
    assert widget_state(preview) == (False, False)
    preview.close()  # idempotent

    count = e.get_num_modules()
    dropped = core("MIDI-Map").create_module()  # its constructor adds param handles to Rack's engine
    del dropped
    gc.collect()  # deleted on the main thread, outside the engine lock
    assert e.get_num_modules() == count

    module = blank.create_module()
    assert isinstance(module, EngineModule)
    e.add_module(module)
    widget = blank.create_module_widget(module)
    assert isinstance(widget, app.ForeignModuleWidget)
    widget.close()  # deletes its module too
    assert not alive(module)

    loose = blank.create_module()
    assert isinstance(loose, EngineModule)
    e.add_module(loose)
    e.remove_module(loose)  # no widget, no cables: Python owns it again
    assert not in_engine(loose) and alive(loose)


def test_a_rackpy_preview_never_enters_the_rack() -> None:
    model = raw.plugin.get_model("RackPy", "RuntimeProbe")
    assert model is not None
    preview = model.create_module_widget()  # a C++ model in RackPy's test plugin: Rack's own widget
    assert isinstance(preview, app.ForeignModuleWidget)
    with pytest.raises(OwnershipError, match="preview"):
        rack().add_module_at_mouse(preview)
    preview.close()
