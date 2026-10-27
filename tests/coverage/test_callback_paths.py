# SPDX-License-Identifier: GPL-3.0-or-later
"""The RP-22D per-virtual execution inventory must not drift or hide native-only gaps."""

import binding_manifest as bm
import generate_bindings as generator
import pytest

pytestmark = pytest.mark.unit


def test_every_public_virtual_has_one_generated_execution_row() -> None:
    inputs = bm.load_inputs()
    manifest = bm.load_json(bm.MANIFEST)
    text = generator.callback_paths(manifest, inputs)
    rows = bm.callback_matrix().classify(inputs.inventory["entries"], bm.load_json(bm.CALLBACK_RULES)["rules"])
    public_virtuals = {
        e["id"] for e in inputs.inventory["entries"] if e.get("virtual") and e["classification"] == "public"
    }
    assert len(rows) == len(public_virtuals) == 434
    assert {r["id"] for r in rows} == public_virtuals
    for row in rows:
        assert text.count(f"| `{row['id']}` | `{row['strategy']}` |") == 1
    assert (bm.REPO / "docs/reference/callback-paths.md").read_text() == text


def test_native_driver_adapters_remain_release_blockers_without_a_python_path() -> None:
    records = bm.load_json(bm.MANIFEST)["entries"]
    drivers = [r for r in records if r["callback"] == "driver"]
    assert len(drivers) == 42
    native_only = [
        r for r in drivers if "rack::engine::Module::" not in r["id"] and "rack::audio::Port::" not in r["id"]
    ]
    assert len(native_only) == 33
    for record in native_only:
        assert record["status"] == "planned"
        assert record["notes"].startswith(
            "Responsible task: RP-22F (tk-rackpy-22f-laboratory-python-paths-for-audio-and-65ccf3): "
            "laboratory Python path."
        )
        assert record["python"] and record["tests"] and record["docs"]  # native adapter evidence is retained
        assert "Full-release blocker" in generator.callback_execution(record, {"name": record["id"].split(":", 1)[1]})
    ports = [r for r in drivers if "rack::audio::Port::" in r["id"]]
    assert len(ports) == 5
    for record in ports:
        assert record["status"] == "implemented"
        assert "Python-driven device" in generator.callback_execution(record, {"name": record["id"].split(":", 1)[1]})


def test_host_only_app_widgets_map_to_calls_on_racks_own_instances() -> None:
    """RP-28F decision: no plugin subclasses these, so the borrowed views' calls are their mappings."""
    inputs = bm.load_inputs()
    records = bm.load_json(bm.MANIFEST)["entries"]
    private = {"RackScrollWidget": "19-20", "RackWidget": "28-29", "Scene": "27-28"}
    rp28d = "RP-28D (tk-rackpy-28d-scene-rack-scroll-rack-s-cables-and-l-3d7ead): Rack's implementation"
    in_rack = "tests/app/in_rack/test_rack.py::test_racks_own_scene_rack_and_plugs_are_not_subclassable"
    host_only = [
        r
        for r in records
        if r["callback"] == "ui" and any("rack::app::" + owner + "::" in r["id"] for owner in ("PlugWidget", *private))
    ]
    assert len(host_only) == 21
    for record in host_only:
        owner = record["id"].split("rack::app::", 1)[1].split("::", 1)[0]
        assert record["status"] == "implemented"
        assert record["notes"].startswith("RP-28F decision (2026-10-26, owner review pending): ")
        if owner == "PlugWidget":
            assert "include/app/CableWidget.hpp:20-21" in record["notes"]
            assert "src/app/CableWidget.cpp:140-151" in record["notes"]
        else:
            assert f"PRIVATE (include/app/{owner}.hpp:{private[owner]})" in record["notes"]
        assert rp28d in record["notes"] and in_rack in record["tests"]
        assert record["python"]["raw"].startswith("rackpy.raw.widget.HostWidget.") and record["docs"]
        assert generator.callback_execution(record, inputs.index[record["id"]]).startswith(
            "Call on Rack's own instance: rackpy.raw.widget.HostWidget."
        )
    cables = [r for r in records if r["callback"] == "ui" and "rack::app::CableWidget::" in r["id"]]
    assert len(cables) == 5
    for record in cables:
        assert record["status"] == "implemented" and "RP-28E" in record["notes"]


def test_laboratory_paths_are_not_mistaken_for_production_overrides() -> None:
    inputs = bm.load_inputs()
    records = bm.load_json(bm.MANIFEST)["entries"]
    laboratory = [r for r in records if r["callback"] in {"engine", "mixed"}]
    assert len(laboratory) == 16
    for record in laboratory:
        text = generator.callback_execution(record, inputs.index[record["id"]])
        assert "Synchronous offline override: rackpy.laboratory." in text
    lifecycle = [r for r in records if r["callback"] == "lifecycle"]
    assert len(lifecycle) == 33
    for record in lifecycle:
        assert "not a Python destructor override" in generator.callback_execution(record, inputs.index[record["id"]])
