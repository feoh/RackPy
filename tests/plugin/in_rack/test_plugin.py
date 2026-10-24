# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-16A: plugins and models inside Rack, after this plugin has loaded (live UI suite)."""

import threading
from collections.abc import Callable

import pytest

import rackpy.plugin
from rackpy import raw
from rackpy.errors import OwnershipError, ThreadError
from rackpy.raw import plugin as rp
from rackpy.raw.engine import Module


def test_this_plugin_after_initialization() -> None:
    this = rp.plugin_instance()
    assert this is rp.get_plugin("RackPy") is rackpy.plugin.plugin.raw  # one handle per plugin
    assert this.alive and this.slug == "RackPy" and this.version == "2.0.0"
    assert this.license == "GPL-3.0-or-later" and this.brand == "RackPy" == this.get_brand()
    assert this.source_url == "https://github.com/feoh/RackPy" and this.path.endswith("RackPy")
    assert isinstance(this.modified_timestamp, float)
    probe = this.get_model("RuntimeProbe")
    assert probe is not None and probe in this.models and probe is rp.get_model("RackPy", "RuntimeProbe")
    assert probe.plugin is this and probe.slug == "RuntimeProbe"
    assert probe.name == "Runtime probe" and probe.description == "RackPy test hook"  # from plugin.json
    assert probe.get_full_name() == "RackPy Runtime probe"
    assert probe.get_factory_preset_directory().endswith("presets/RuntimeProbe")
    assert "RackPy" in probe.get_user_preset_directory() and probe.get_manual_url() == this.manual_url
    assert this.get_model("NoSuchModel") is None and rp.get_plugin("NoSuchPlugin") is None


def test_models_are_added_only_while_the_plugin_initializes() -> None:
    model = rp.create_model("Late", lambda m: None)
    assert model.alive and model.plugin is None and model.slug == "Late"
    model.slug = "LateRenamed"  # a detached model's slug may still change
    with pytest.raises(OwnershipError, match="while the plugin initializes"):
        rp.plugin_instance().add_model(model)
    assert model.alive and model.plugin is None  # a refused model stays Python's
    with pytest.raises(OwnershipError, match="while the plugin initializes"):
        rackpy.plugin.plugin.add_model("AlsoLate", lambda m: None)
    with pytest.raises(OwnershipError, match="add the model to a plugin first"):
        model.get_full_name()
    assert rp.get_model("RackPy", "LateRenamed") is None
    core = rp.get_plugin("Core")
    assert core is not None
    with pytest.raises(OwnershipError, match="only to this plugin"):
        core.add_model(rp.create_model("Elsewhere", lambda m: None))


def test_create_model_checks_its_arguments() -> None:
    with pytest.raises(TypeError, match="callable"):
        rp.create_model("Bad", 42)  # type: ignore[arg-type]
    for hp in (0, 129):
        with pytest.raises(ValueError, match="hp"):
            rp.create_model("Bad", lambda m: None, hp=hp)


def test_model_fields_are_this_plugins_to_change() -> None:
    probe = rp.get_model("RackPy", "RuntimeProbe")
    assert probe is not None
    name, tags, hidden = probe.name, probe.tag_ids, probe.hidden
    try:
        probe.name, probe.tag_ids, probe.hidden = "Renamed probe", [1, 2], True
        assert (probe.name, probe.tag_ids, probe.hidden) == ("Renamed probe", [1, 2], True)
        assert probe.get_full_name() == "RackPy Renamed probe"
    finally:
        probe.name, probe.tag_ids, probe.hidden = name, tags, hidden
    with pytest.raises(OwnershipError, match="cannot change once it is added"):
        probe.slug = "Other"
    core = rp.get_plugin("Core")
    assert core is not None and core.models
    foreign = core.models[0]
    assert foreign.plugin is core and foreign.name
    for field, value in (("name", "x"), ("description", "x"), ("hidden", True), ("tag_ids", [0])):
        with pytest.raises(OwnershipError, match="read-only"):
            setattr(foreign, field, value)


def test_favorites_and_lookups_follow_rack() -> None:
    probe = rp.get_model("RackPy", "RuntimeProbe")
    assert probe is not None
    before = probe.is_favorite()
    try:
        probe.set_favorite(not before)
        assert probe.is_favorite() is (not before)
    finally:
        probe.set_favorite(before)
    assert rp.is_slug_valid("Py-Gain_2") and not rp.is_slug_valid("bad slug")
    assert rp.is_slug_valid("")  # Rack's character rule only; add_model refuses an empty slug itself
    assert rp.normalize_slug("bad slug!") == "badslug"
    assert rp.model_from_json({"plugin": "RackPy", "model": "RuntimeProbe"}) is probe
    with pytest.raises(raw.Exception):
        rp.model_from_json({"plugin": "RackPy", "model": "Missing"})
    assert rp.get_plugin_fallback("RackPy") is rp.plugin_instance()
    assert rp.get_model_fallback("RackPy", "RuntimeProbe") is probe


def test_plugin_and_model_main_thread_rule() -> None:
    this = rp.plugin_instance()
    probe = this.get_model("RuntimeProbe")
    assert probe is not None
    actions: list[Callable[[], object]] = [
        lambda: this.slug,
        lambda: probe.name,
        rp.plugin_instance,
        lambda: rp.get_model("RackPy", "RuntimeProbe"),
        lambda: rp.create_model("Worker", lambda m: None),
        lambda: len(rp.plugins),
        lambda: rp.on_destroy(lambda: None),
        lambda: raw.asset.plugin(this),
    ]
    errors: list[BaseException] = []

    def worker() -> None:
        for action in actions:
            try:
                action()
            except BaseException as error:
                errors.append(error)

    thread = threading.Thread(target=worker)
    thread.start()
    thread.join()
    assert len(errors) == len(actions) and all(isinstance(e, ThreadError) for e in errors)


def test_a_detached_model_dies_with_its_handle() -> None:
    import gc
    import weakref

    class Factory:
        def __call__(self, module: Module) -> None:
            pass

    factory = Factory()
    marker = weakref.ref(factory)
    model = rp.create_model("Dropped", factory)
    del factory
    gc.collect()
    assert marker() is not None  # the model holds its factory
    del model
    gc.collect()
    assert marker() is None  # deleting the model released it


def test_pure_slug_and_asset_functions_work_on_any_thread() -> None:
    results: list[object] = []
    thread = threading.Thread(
        target=lambda: results.extend([rp.is_slug_valid("a"), rp.normalize_slug("a b"), raw.asset.system("x")])
    )
    thread.start()
    thread.join()
    assert results[:2] == [True, "ab"] and str(results[2]).endswith("/x")


def test_factory_signature_matches_module_create() -> None:
    seen: list[Module] = []
    model = rp.create_model("Signature", seen.append)
    assert model.alive and seen == []  # nothing runs until Rack creates a module


def test_assets_are_rack_paths() -> None:
    from pathlib import Path

    import rackpy.assets
    from rackpy.raw import asset

    this = rp.plugin_instance()
    assert asset.plugin(this, "res/x.json") == this.path + "/res/x.json" and asset.plugin(this) == this.path + "/"
    assert asset.system("res") == asset.systemDir + "/res" and Path(asset.systemDir).is_dir()
    assert asset.user("autosave") == asset.userDir + "/autosave" and Path(asset.userDir).is_dir()
    assert isinstance(asset.oldUserDir, str) and asset.bundlePath == ""  # bundlePath is set on macOS only
    assert rackpy.assets.plugin("res/x.json") == Path(this.path) / "res" / "x.json"
    assert rackpy.assets.system() == Path(asset.systemDir) and rackpy.assets.user("a") == Path(asset.userDir) / "a"


def test_plugins_list_path_and_library_handles() -> None:
    from pathlib import Path

    this = rp.plugin_instance()
    plugins = rp.plugins
    assert len(plugins) >= 2 and this in plugins and plugins[0].slug == "Core"  # Rack loads Core first
    assert list(plugins) == list(plugins[:]) and plugins[-1] is plugins[len(plugins) - 1]
    assert rp.get_plugin("Core") in plugins and "Core" not in plugins  # type: ignore[comparison-overlap]
    with pytest.raises(IndexError):
        plugins[len(plugins)]
    assert Path(rp.pluginsPath).is_dir() and this.path.startswith(rp.pluginsPath)
    handle = this.handle
    assert handle is not None and handle is this.handle and isinstance(handle, rp.LibraryHandle)
    assert plugins[0].handle is None  # Core is part of Rack, not a library
    for attribute in ("__int__", "__index__", "address"):
        assert not hasattr(handle, attribute)


def test_model_from_json_takes_a_manifest_entry() -> None:
    probe = rp.get_model("RackPy", "RuntimeProbe")
    assert probe is not None
    saved = (probe.name, probe.description, probe.tag_ids, probe.hidden)
    try:
        entry = {"name": "Probe from JSON", "description": "d", "tags": ["Utility", "nonsense"], "hidden": True}
        probe.from_json(entry)
        assert (probe.name, probe.description, probe.hidden) == ("Probe from JSON", "d", True)
        assert len(probe.tag_ids) == 1  # Rack drops unknown tags
        for bad in ({"name": 3}, {"tags": "Utility"}, {"tags": [1]}, {"hidden": "yes"}, ["not", "a", "dict"]):
            with pytest.raises(TypeError):
                probe.from_json(bad)  # type: ignore[arg-type]
        with pytest.raises(raw.Exception, match="has no name"):
            probe.from_json({"name": ""})
    finally:
        probe.name, probe.description, probe.tag_ids, probe.hidden = saved
    core = rp.get_plugin("Core")
    assert core is not None
    with pytest.raises(OwnershipError, match="read-only"):
        core.models[0].from_json({"name": "x"})
    with pytest.raises(OwnershipError, match="add the model to a plugin first"):
        rp.create_model("Detached", lambda m: None).from_json({"name": "x"})


def test_module_class_builds_modules_and_owns_their_state() -> None:
    import gc
    import weakref
    from typing import Any

    from rackpy.plugin import ModuleClass

    instances: list[weakref.ref[ModuleClass]] = []

    class Notes(ModuleClass):
        def configure(self) -> None:
            self.module.config(1, 0, 0, 0)
            self.module.config_param(0, 0, 1, 0.5, "Mix")
            self.text = ""
            instances.append(weakref.ref(self))

        def to_json(self) -> Any:
            return {"text": self.text}

        def from_json(self, data: Any) -> None:
            if isinstance(data, dict) and isinstance(data.get("text"), str):
                self.text = data["text"]

    m = Module.create(Notes.factory())
    try:
        assert m.get_num_params() == 1 and m.data_to_json() == {"text": ""}
        m.data_from_json({"text": "kept"})
        assert m.data_to_json() == {"text": "kept"}
        gc.collect()
        assert instances[0]() is not None  # the module keeps its instance
    finally:
        m.close()
    del m
    gc.collect()
    assert instances[0]() is None

    plain = Module.create(ModuleClass.factory())
    try:
        assert plain.data_to_json() is None  # the defaults save nothing, as Rack's do
    finally:
        plain.close()
    with pytest.raises(TypeError, match="ModuleClass"):
        rackpy.plugin.plugin.add_model("NotAModuleClass", int)  # type: ignore[arg-type]


def test_loader_methods_are_refused_after_initialization() -> None:
    this = rp.plugin_instance()
    with pytest.raises(OwnershipError, match="only while this plugin initializes"):
        this.from_json({"name": "Late"})
    with pytest.raises(OwnershipError, match="only while this plugin initializes"):
        this.modules_from_json([])
    core = rp.get_plugin("Core")
    assert core is not None
    with pytest.raises(OwnershipError, match="only this plugin"):
        core.from_json({"name": "x"})


def test_on_destroy_is_a_decorator() -> None:
    def cleanup() -> None:
        pass

    assert rackpy.plugin.on_destroy(cleanup) is cleanup
    with pytest.raises(TypeError):
        rp.on_destroy(42)  # type: ignore[type-var]
