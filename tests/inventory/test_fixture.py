# SPDX-License-Identifier: GPL-3.0-or-later
"""Inventory behaviour on the miniature fixture SDK: IDs, classification, mutations, failures."""

import re
from collections.abc import Callable
from pathlib import Path
from types import ModuleType
from typing import Any

import pytest

pytestmark = pytest.mark.inventory

Build = Callable[[Path], dict[str, Any]]


def by_id(doc: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {e["id"]: e for e in doc["entries"]}


def edit(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    assert old in text
    path.write_text(text.replace(old, new, 1))


def test_records_overloads_and_flags(minisdk: Path, build: Build) -> None:
    e = by_id(build(minisdk))
    assert e["record:mini::shapes::Box"]["classification"] == "public"
    assert "constructor:mini::shapes::Box::Box()" in e
    assert e["constructor:mini::shapes::Box::Box(float)"].get("explicit")
    assert "method:mini::shapes::Box::scale(float)" in e
    assert "method:mini::shapes::Box::scale(float, float)" in e
    area = e["method:mini::shapes::Box::area() const"]
    assert area.get("virtual") and area["returns"] == "float"
    assert e["method:mini::shapes::Box::count()"].get("static")
    assert e["method:mini::shapes::Box::resize(float)"].get("deprecated")


def test_private_pimpl_and_opaque(minisdk: Path, build: Build) -> None:
    e = by_id(build(minisdk))
    reset = e["method:mini::shapes::Box::reset()"]
    assert reset["classification"] == "excluded_internal" and reset["evidence"] == "PRIVATE attribute"
    assert e["field:mini::shapes::Box::internal"]["classification"] == "excluded_internal"
    assert e["opaque_record:mini::shapes::Box::Internal"]["classification"] == "excluded_internal"
    assert e["opaque_record:mini::shapes::Hidden"]["classification"] == "public_opaque"


def test_anonymous_union_members_and_comment_annotations(minisdk: Path, build: Build) -> None:
    e = by_id(build(minisdk))
    data, legacy = e["field:mini::shapes::Box::data"], e["field:mini::shapes::Box::legacy"]
    assert data["anonymous_group"] == legacy["anonymous_group"]
    assert data.get("unstable") and data["type"] == "float[2]"
    assert legacy.get("comment_deprecated") and not legacy.get("deprecated")


def test_templates_defaults_and_specializations(minisdk: Path, build: Build) -> None:
    e = by_id(build(minisdk))
    buffer = e["class_template:mini::shapes::Buffer<type, value>"]
    assert [(p["kind"], p["default"]) for p in buffer["template_params"]] == [("type", "float"), ("value", "4")]
    assert "specialization:mini::shapes::Buffer<int, 2>" in e
    get = e["method:mini::shapes::Buffer::get(int) const"]
    assert get["returns"] == "$c0", "class template parameters are positional in IDs"
    assert "function_template:mini::shapes::convert<type>($0)" in e
    assert e["function:mini::shapes::convert<float>(float)"]["specialization_args"] == ["float"]


def test_enums_aliases_macros_and_imports(minisdk: Path, build: Build) -> None:
    e = by_id(build(minisdk))
    assert e["enumerator:mini::shapes::Color::GREEN"]["value"] == 5
    assert e["enumerator:mini::shapes::Color::RED"]["visible_as"] == "mini::shapes::RED"
    assert e["alias:mini::shapes::BoxAlias"]["target"] == "mini::shapes::Box"
    assert e["macro:MINI_LIMIT"]["kind"] == "object_macro" and e["macro:MINI_LIMIT"]["body"] == "16"
    twice = e["macro:MINI_TWICE"]
    assert twice["kind"] == "function_macro" and twice["params"] == ["x"]
    assert e["macro:PRIVATE"]["conditions"] == ["defined __clang__"]
    imp = e["import:mini:using namespace shapes"]
    assert imp["classification"] == "import"


def test_dependency_event_constants_are_counted_only_when_reached(minisdk: Path, build: Build) -> None:
    dep = minisdk / "dep" / "include"
    (dep / "GLFW").mkdir(parents=True)
    (dep / "GLFW" / "glfw3.h").write_text(
        "#define GLFW_KEY_A 65\n#define GLFW_KEY_LAST GLFW_KEY_A\n#define GLFW_PRESS 1\n#define GLFW_OTHER 7\n"
    )
    (dep / "blendish.h").write_text(
        'extern "C" { typedef enum BNDcornerFlags { BND_CORNER_NONE = 0, BND_CORNER_TOP = 3 } BNDcornerFlags; }\n'
    )
    before = by_id(build(minisdk))
    assert "boundary_constant:GLFW_KEY_A" not in before
    with (minisdk / "include" / "rack.hpp").open("a") as rack:
        rack.write("\n#include <GLFW/glfw3.h>\n#include <blendish.h>\n")
    after = by_id(build(minisdk))
    assert after["boundary_constant:GLFW_KEY_A"]["body"] == "65"
    assert after["boundary_constant:GLFW_KEY_LAST"]["body"] == "GLFW_KEY_A"
    assert after["boundary_constant:GLFW_PRESS"]["header"] == "dep/include/GLFW/glfw3.h"
    assert after["boundary_constant:BND_CORNER_TOP"]["value"] == 3
    assert "boundary_constant:GLFW_OTHER" not in after


def test_platform_branch_is_recorded_not_dropped(minisdk: Path, build: Build) -> None:
    doc = build(minisdk)
    e = by_id(doc)
    assert "function:mini::everywhere()" in e
    assert not any("windowsOnly" in i for i in e)
    (block,) = [b for b in doc["conditional_blocks"] if b["header"] == "include/platform.hpp"]
    assert block["condition"] == "#if defined _WIN32"
    assert block["declarations"] == ["function windowsOnly"]
    assert block["skipped_in"] == ["lin-x64"]


def test_two_runs_are_byte_identical(minisdk: Path, build: Build, inventory: ModuleType) -> None:
    assert inventory.dumps(build(minisdk)) == inventory.dumps(build(minisdk))


@pytest.mark.parametrize(
    ("header", "old", "new", "key", "expected"),
    [
        (
            "shapes.hpp",
            "\tstatic int count();",
            "\tstatic int count();\n\tvoid scale(double s);",
            "added",
            "method:mini::shapes::Box::scale(double)",
        ),
        ("shapes.hpp", "#define MINI_LIMIT 16", "#define MINI_LIMIT 16\n#define MINI_NEW 1", "added", "macro:MINI_NEW"),
        (
            "shapes.hpp",
            "\tvoid scale(float sx, float sy);\n",
            "",
            "removed",
            "method:mini::shapes::Box::scale(float, float)",
        ),
        (
            "shapes.hpp",
            "explicit Box(float w);",
            "Box(float w);",
            "changed",
            "constructor:mini::shapes::Box::Box(float)",
        ),
    ],
)
def test_mutations_are_detected(
    minisdk: Path, build: Build, inventory: ModuleType, header: str, old: str, new: str, key: str, expected: str
) -> None:
    before = build(minisdk)
    edit(minisdk / "include" / header, old, new)
    delta = inventory.diff(before, build(minisdk))
    assert expected in delta[key], delta


def test_new_platform_branch_is_detected(minisdk: Path, build: Build, inventory: ModuleType) -> None:
    before = build(minisdk)
    edit(
        minisdk / "include" / "platform.hpp",
        "void everywhere();",
        "#if defined __APPLE__\nvoid macOnly();\n#endif\n\nvoid everywhere();",
    )
    delta = inventory.diff(before, build(minisdk))
    assert delta["conditional_blocks_added"] and not delta["added"]


def test_renamed_template_parameter_and_moved_header_keep_ids(
    minisdk: Path, build: Build, inventory: ModuleType
) -> None:
    before = build(minisdk)
    shapes = minisdk / "include" / "shapes.hpp"
    text = re.sub(r"\bT\b", "TValue", shapes.read_text())
    (minisdk / "include" / "geometry").mkdir()
    (minisdk / "include" / "geometry" / "shapes.hpp").write_text(text)
    shapes.unlink()
    edit(minisdk / "include" / "rack.hpp", "#include <shapes.hpp>", "#include <geometry/shapes.hpp>")
    delta = inventory.diff(before, build(minisdk))
    assert not delta["added"] and not delta["removed"], delta
    assert delta["changed"], "the header path change must still be visible"


def test_parse_error_aborts(minisdk: Path, build: Build, inventory: ModuleType) -> None:
    edit(minisdk / "include" / "platform.hpp", "void everywhere();", "void everywhere(")
    with pytest.raises(inventory.ExtractionError, match="error"):
        build(minisdk)


def test_parse_error_writes_nothing(minisdk: Path, inventory: ModuleType, tmp_path: Path) -> None:
    edit(minisdk / "include" / "platform.hpp", "void everywhere();", "void everywhere(")
    out = tmp_path / "surface.json"
    with pytest.raises(inventory.ExtractionError):
        inventory.extract(sdk=minisdk, output=out, variants=("lin-x64",))
    assert not out.exists()


def test_empty_surface_is_refused(minisdk: Path, build: Build, inventory: ModuleType) -> None:
    (minisdk / "include" / "rack.hpp").write_text("#pragma once\n")
    with pytest.raises(inventory.ExtractionError, match="no declarations"):
        build(minisdk)


def test_id_collision_is_an_error(minisdk: Path, build: Build, inventory: ModuleType) -> None:
    """Two in-scope declarations with one ID would hide one of them, so extraction stops for review."""
    edit(minisdk / "include" / "platform.hpp", "void everywhere();", "void everywhere();\nvoid everywhere();")
    with pytest.raises(inventory.ExtractionError, match="share an ID"):
        build(minisdk)
