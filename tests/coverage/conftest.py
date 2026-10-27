# SPDX-License-Identifier: GPL-3.0-or-later
"""A miniature repository for the manifest tests: a tiny inventory, allowlist and evidence files."""

import copy
import json
import sys
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import pytest

from tests.support import REPO

sys.path.insert(0, str(REPO / "bin"))

import binding_manifest as bm


def entry(ident: str, **extra: Any) -> dict[str, Any]:
    kind, _, rest = ident.partition(":")
    name = rest.split("(", 1)[0]
    return {
        "classification": "public",
        "header": "include/engine/Module.hpp",
        "id": ident,
        "kind": kind,
        "line": 1,
        "name": name,
        **extra,
    }


BASE_ENTRIES = [
    entry("method:rack::engine::Module::process(const ProcessArgs &)", virtual=True),
    entry("method:rack::engine::Port::getVoltage(uint8_t)", header="include/engine/Port.hpp"),
    entry("method:rack::engine::Port::getVoltage()", header="include/engine/Port.hpp"),
    entry("method:rack::engine::Engine::addModule(rack::engine::Module *)", header="include/engine/Engine.hpp"),
    entry("method:rack::engine::Engine::getNumModules()", header="include/engine/Engine.hpp"),
    entry(
        "method:rack::engine::Engine::setSampleRate(float)",
        header="include/engine/Engine.hpp",
        classification="excluded_internal",
        evidence="PRIVATE attribute",
    ),
    entry("record:rack::app::SvgPanel", header="include/app/SvgPanel.hpp"),
    entry("alias:rack::app::SVGPanel", header="include/app/SvgPanel.hpp", target="rack::app::SvgPanel"),
    {
        "classification": "import",
        "header": "include/rack.hpp",
        "id": "import:rack:using plugin::Model",
        "kind": "import",
        "line": 2,
        "name": "rack",
    },
]


@dataclass
class Mini:
    repo: Path
    inventory: dict[str, Any]
    exclusions: dict[str, dict[str, Any]]
    strategies: dict[str, str | None] = field(default_factory=dict)
    locks: dict[str, str] = field(default_factory=dict)

    def inputs(self) -> bm.Inputs:
        text = json.dumps(self.inventory, sort_keys=True)
        import hashlib

        return bm.Inputs(
            inventory=self.inventory,
            inventory_sha256=hashlib.sha256(text.encode()).hexdigest(),
            exclusions=self.exclusions,
            strategies=self.strategies,
            locks=self.locks,
            repo=self.repo,
        )

    def seeded(self) -> dict[str, Any]:
        manifest, _ = bm.seed(self.inputs(), None)
        return manifest

    def write(self, rel: str, text: str) -> Path:
        path = self.repo / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path


@pytest.fixture
def mini(tmp_path: Path) -> Mini:
    inventory = {
        "schema_version": 1,
        "sdk": {"rack_commit": "0" * 40},
        "families": {
            "include/engine/Module.hpp": "2 engine",
            "include/engine/Port.hpp": "2 engine",
            "include/engine/Engine.hpp": "2 engine",
            "include/app/SvgPanel.hpp": "4 widgets/ui/app/componentlibrary",
            "include/rack.hpp": "0 entry point",
        },
        "extractions": [{"variant": "lin-x64", "status": "ok"}],
        "conditional_blocks": [],
        "entries": copy.deepcopy(BASE_ENTRIES),
    }
    exclusions = {
        "method:rack::engine::Engine::setSampleRate(float)": {
            "id": "method:rack::engine::Engine::setSampleRate(float)",
            "evidence": "PRIVATE attribute",
            "reason": "PRIVATE (policy 2)",
            "reviewer": "test",
        }
    }
    m = Mini(tmp_path, inventory, exclusions)
    m.strategies = {"method:rack::engine::Module::process(const ProcessArgs &)": "engine"}
    m.locks = {
        "method:rack::engine::Engine::addModule(rack::engine::Module *)": "exclusive",
        "method:rack::engine::Engine::getNumModules()": "none",
    }
    (tmp_path / "src" / "rackpy").mkdir(parents=True)
    (tmp_path / "src" / "rackpy" / "__init__.py").write_text("")
    return m


def bind(record: dict[str, Any], mini: Mini, *, raw: str, kind: str = "method", **python: Any) -> dict[str, Any]:
    """Give a record complete, existing evidence: an implementation file, a test and a docs anchor."""
    mini.write("native/bindings/engine.cpp", "// adapter\n")
    mini.write("tests/runtime/test_bound.py", "def test_bound():\n    pass\n")
    mini.write("docs/reference/engine.md", "# Engine\n\n## Port voltages\n")
    record.update(
        status="implemented",
        python={"raw": raw, "kind": kind, **python},
        implementation="native/bindings/engine.cpp",
        tests=["tests/runtime/test_bound.py::test_bound"],
        docs=["docs/reference/engine.md#port-voltages"],
    )
    return record


def codes(problems: list[bm.Problem]) -> set[str]:
    return {p.code for p in problems}


Codes = Callable[[list[bm.Problem]], set[str]]
