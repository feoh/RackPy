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


def test_factory_signature_matches_module_create() -> None:
    seen: list[Module] = []
    model = rp.create_model("Signature", seen.append)
    assert model.alive and seen == []  # nothing runs until Rack creates a module
