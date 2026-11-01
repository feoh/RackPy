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


def test_every_advertised_platform_was_extracted(surface: dict[str, Any]) -> None:
    # RP-42: macOS with libc++ and Apple's C headers (as zig ships them), Windows with Rack's own MinGW toolchain;
    # each records which target headers it read, by digest, not by this machine's paths.
    runs = {r["variant"]: r for r in surface["extractions"]}
    assert set(runs) == {"lin-x64", "lin-x64-simde", "mac-arm64", "mac-x64", "win-x64"}
    assert all(r["status"] == "ok" for r in runs.values()), runs
    for variant in ("mac-arm64", "mac-x64", "win-x64"):
        assert runs[variant]["system_headers_read"] > 100 and len(runs[variant]["system_headers_sha256"]) == 64
        assert "<" in runs[variant]["system_headers"] and "/home/" not in json.dumps(runs[variant])
    for r in runs.values():
        assert "-O3" in r["flags"], r  # compile.mk's: mingw's string.h renames strcasecmp without optimization


def test_platform_only_declarations(entries: dict[str, dict[str, Any]]) -> None:
    def only(*variants: str) -> set[str]:
        everywhere = {"lin-x64", "lin-x64-simde", "mac-arm64", "mac-x64", "win-x64"}
        return {
            i
            for i, e in entries.items()
            if e["classification"] != "import" and everywhere - set(e.get("absent_in", ())) == set(variants)
        }

    assert only("win-x64") == {
        "function:fopen_u8(const char *, const char *)",
        "function:rack::string::UTF16toUTF8(const std::wstring &)",
        "function:rack::string::UTF8toUTF16(const std::string &)",
        "macro:ARCH_WIN",
        "macro:fopen",
    }
    assert only("mac-arm64", "mac-x64") == {"macro:ARCH_MAC"}
    assert only("mac-arm64") == {"macro:ARCH_ARM64"}
    assert only("lin-x64", "lin-x64-simde") == {"macro:ARCH_LIN"}
    # libRack.dll exports rack::string::strcasecmp: with -O3 it is not mingw's _stricmp macro.
    assert "absent_in" not in entries["function:rack::string::strcasecmp(const char *, const char *)"]
    ctrl = entries["macro:RACK_MOD_CTRL"]
    assert ctrl["body"] == "GLFW_MOD_CONTROL" and ctrl["per_variant"]["mac-arm64"]["body"] == "GLFW_MOD_SUPER"
    assert set(ctrl["per_variant"]) == {"mac-arm64", "mac-x64"}


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

    # The SSE4.2 and SIMDe paths expose the same rack:: declarations; only SIMDe's own macros differ, on Linux
    # (lin-x64 against lin-x64-simde) and between the two Macs (and there the ARCH_ macros).
    def between(a: str, b: str) -> list[str]:
        return sorted(
            e["id"] for e in surface["entries"] if (a in e.get("absent_in", ())) != (b in e.get("absent_in", ()))
        )

    simde = ["macro:SIMDE_ENABLE_NATIVE_ALIASES", "macro:_MM_FROUND_NO_EXC"]
    assert between("lin-x64", "lin-x64-simde") == simde
    assert between("mac-x64", "mac-arm64") == ["macro:ARCH_ARM64", "macro:ARCH_X64", *simde]


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
