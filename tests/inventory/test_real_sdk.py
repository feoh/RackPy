# SPDX-License-Identifier: GPL-3.0-or-later
"""Spot checks of the committed inventory against the pinned Rack 2.6.6 SDK."""

import hashlib
import json
from pathlib import Path
from types import ModuleType
from typing import Any

import pytest

from tests.support import require

pytestmark = pytest.mark.inventory

REPO = Path(__file__).resolve().parents[2]
SURFACE = REPO / "upstream" / "api-surface.json"


@pytest.fixture(scope="module")
def surface() -> dict[str, Any]:
    doc: dict[str, Any] = json.loads(SURFACE.read_text(encoding="utf-8"))
    return doc


@pytest.fixture(scope="module")
def entries(surface: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {e["id"]: e for e in surface["entries"]}


def test_committed_inventory_is_current(inventory: ModuleType, real_sdk: Path, surface: dict[str, Any]) -> None:
    # The inventory records the exact libclang it was extracted with, so only that build can reproduce it.
    libclang: Path = inventory.find_libclang("clang++")
    recorded = surface["toolchain"]
    same = libclang.is_file() and hashlib.sha256(libclang.read_bytes()).hexdigest() == recorded["libclang_sha256"]
    require(
        "pinned-libclang",
        same,
        f"upstream/api-surface.json was extracted with {recorded['libclang']} (libclang.so sha256 "
        f"{recorded['libclang_sha256'][:12]}...), and {libclang} differs. Run this on a machine with that libclang, "
        "or re-extract with bin/inventory_sdk.py extract and review the diff",
    )
    variants = tuple(r["variant"] for r in surface["extractions"])
    fresh = inventory.build(real_sdk, None, "clang++", variants)
    assert inventory.dumps(fresh) == SURFACE.read_text(encoding="utf-8"), inventory.diff(surface, fresh)


def test_native_extractions_succeeded_and_gaps_are_explicit(surface: dict[str, Any]) -> None:
    status = {r["variant"]: r["status"] for r in surface["extractions"]}
    assert status["lin-x64"] == "ok" and status["lin-x64-simde"] == "ok"
    for gap in ("mac-arm64", "mac-x64", "win-x64"):
        run = next(r for r in surface["extractions"] if r["variant"] == gap)
        assert run["status"] == "ok" or run["reason"], run


def test_module(entries: dict[str, dict[str, Any]]) -> None:
    process = entries["method:rack::engine::Module::process(const rack::engine::Module::ProcessArgs &)"]
    assert process.get("virtual") and process["classification"] == "public"
    assert (
        entries["method:rack::engine::Module::doProcess(const rack::engine::Module::ProcessArgs &)"]["classification"]
        == "excluded_internal"
    )
    step = entries["method:rack::engine::Module::step()"]
    assert step.get("comment_deprecated") and step.get("virtual")


def test_widget(entries: dict[str, dict[str, Any]]) -> None:
    current = entries["method:rack::widget::Widget::draw(const rack::widget::Widget::DrawArgs &)"]
    legacy = entries["method:rack::widget::Widget::draw(NVGcontext *)"]
    assert current.get("virtual") and not current.get("deprecated")
    assert legacy.get("deprecated") and "nanovg.h:NVGcontext" in legacy["types"]


def test_dsp_templates(entries: dict[str, dict[str, Any]]) -> None:
    trigger = entries["class_template:rack::dsp::TSchmittTrigger<type>"]
    assert trigger["template_params"][0]["default"] == "float"
    assert "specialization:rack::dsp::TSchmittTrigger<float>" in entries
    ring = entries["class_template:rack::dsp::RingBuffer<type, value>"]
    assert [p["kind"] for p in ring["template_params"]] == ["type", "value"]


def test_simd(entries: dict[str, dict[str, Any]], surface: dict[str, Any]) -> None:
    assert entries["opaque_record:rack::simd::Vector<type, value>"]["classification"] == "public_opaque"
    assert "specialization:rack::simd::Vector<float, 4>" in entries
    assert "specialization:rack::simd::Vector<int32_t, 4>" in entries
    assert "constructor:rack::simd::Vector<float, 4>::Vector(float, float, float, float)" in entries
    # The SSE4.2 and SIMDe paths expose the same rack:: declarations; only SIMDe's own macros differ.
    differing = sorted(e["id"] for e in surface["entries"] if e.get("absent_in"))
    assert differing == ["macro:SIMDE_ENABLE_NATIVE_ALIASES", "macro:_MM_FROUND_NO_EXC"]


def test_context(entries: dict[str, dict[str, Any]]) -> None:
    for name in ("event", "scene", "engine", "window", "history", "patch", "midiLoopbackContext"):
        assert entries[f"field:rack::Context::{name}"]["classification"] == "public"
    assert entries["opaque_record:rack::patch::Manager"]["classification"] == "public_opaque"
    assert entries["macro:APP"]["body"] == "rack :: contextGet ( )"


def test_port_unions(entries: dict[str, dict[str, Any]]) -> None:
    voltages, value = entries["field:rack::engine::Port::voltages"], entries["field:rack::engine::Port::value"]
    assert voltages["anonymous_group"] == value["anonymous_group"]
    assert voltages.get("unstable") and value.get("comment_deprecated")


def test_counts_agree_with_the_rp02_survey(surface: dict[str, Any]) -> None:
    fixture = json.loads((REPO / "tests" / "fixtures" / "sdk-provenance.json").read_text(encoding="utf-8"))
    survey = fixture["preliminary_survey"]
    rows = surface["entries"]
    assert sum(1 for e in rows if e.get("private")) == len(survey["private_declarations"])
    assert sum(1 for e in rows if e.get("deprecated")) == len(survey["deprecated_attribute_declarations"])
    assert sum(1 for e in rows if e["kind"] == "opaque_record") == len(survey["incomplete_rack_records"])


def test_platform_only_declarations_are_kept(surface: dict[str, Any]) -> None:
    windows = [b for b in surface["conditional_blocks"] if b["header"] == "include/string.hpp"]
    assert windows and {"function UTF16toUTF8", "function UTF8toUTF16"} <= set(windows[0]["declarations"])


def test_deprecated_namespace_is_inherited(entries: dict[str, dict[str, Any]]) -> None:
    alias = entries["alias:rack::event::Base"]
    assert alias.get("comment_deprecated") and alias["deprecated_scope"] == "rack::event"


def test_function_template_specializations_have_portable_ids(entries: dict[str, dict[str, Any]]) -> None:
    for t in ("uint8_t", "uint16_t", "uint32_t", "float", "double", "bool"):
        assert f"function:rack::random::get<{t}>()" in entries
