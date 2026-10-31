# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-35: examples/drawing and examples/dark-panels over Rack's real frames.

Rack Free 2.6.6 draws a patch with a Lantern cabled into a Meter under Xvfb with Mesa's
software OpenGL, and tests/examples/entries/drawing_session.py reads the frames back:
the light and dark panels, a dimmed room against the self-lit lamp and bar, the text,
NanoVG shapes, the cached scale's renders, a missing font, a new graphics context, a
deletion and its undo, and a browser preview with no module. Its screenshots are
compared with the reviewed references in docs/_static/examples/, which the walkthrough
shows. Set RACKPY_UPDATE_SCREENSHOTS=1 to rewrite them; the test then fails so that
the new images are reviewed before they are committed.
"""

import json
import os
import shutil
import struct
import zlib
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.examples.support import EXAMPLES, plugin_tool, saved_patch, write_patch
from tests.support import (
    PLUGINS,
    PROBE_MODULE,
    REPO,
    assert_clean_exit,
    rack_platforms,
    run_rack,
    run_rack_windowed,
    stage_plugin,
)

DRAWING, DARK = EXAMPLES / "drawing", EXAMPLES / "dark-panels"
PROJECTS = (DRAWING, DARK)
HERE = Path(__file__).resolve().parent
SHOTS = REPO / "docs" / "_static" / "examples"
SCREENSHOTS = ("lantern-light", "lantern-dark", "lantern-dark-room", "meter", "meter-dark-room", "meter-preview")


def png(width: int, height: int, rgba: bytes) -> bytes:
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    rows = b"".join(b"\0" + rgba[y * width * 4 : (y + 1) * width * 4] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")


def unpng(data: bytes) -> tuple[int, int, bytes]:
    """Reads the PNGs png() makes (8-bit RGBA, unfiltered rows)."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind, body = data[pos + 4 : pos + 8], data[pos + 8 : pos + 8 + length]
        if kind == b"IHDR":
            width, height = struct.unpack(">II", body[:8])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    rows = zlib.decompress(idat)
    stride = width * 4 + 1
    return width, height, b"".join(rows[y * stride + 1 : (y + 1) * stride] for y in range(height))


def opaque(rgba: bytes) -> bytes:
    """Window pixels: the framebuffer's alpha is not the image's, so show them opaque."""
    data = bytearray(rgba)
    data[3::4] = b"\xff" * (len(data) // 4)
    return bytes(data)


def difference(a: bytes, b: bytes) -> tuple[float, float]:
    """Mean absolute channel difference, and the share of pixels with a channel more than 32 apart."""
    total, far = 0, 0
    for i in range(0, len(a), 4):
        channels = [abs(a[i + k] - b[i + k]) for k in range(3)]
        total += sum(channels)
        far += max(channels) > 32
    pixels = len(a) // 4
    return total / (pixels * 3), far / pixels


def near(actual: list[int], expected: tuple[int, int, int], tolerance: int = 10) -> bool:
    return all(abs(a - e) <= tolerance for a, e in zip(actual[:3], expected, strict=True))


def manifest_entries() -> list[dict[str, object]]:
    return [entry for project in PROJECTS for entry in json.loads((project / "plugin.json").read_text())["modules"]]


def stage_examples(bundle: Path) -> Path:
    for project in PROJECTS:
        for item in (project / "pythonpath").iterdir():
            if item.name != "__pycache__":
                (shutil.copytree if item.is_dir() else shutil.copy)(item, bundle / "pythonpath" / item.name)
        shutil.copytree(project / "res", bundle / "res", dirs_exist_ok=True)
    return bundle


@pytest.mark.unit
def test_the_panels_are_original_and_no_font_is_copied() -> None:
    for svg in [*DRAWING.glob("res/*.svg"), *DARK.glob("res/*.svg")]:
        assert "Original RackPy artwork" in svg.read_text(), svg
    light, dark = (DARK / "res" / name for name in ("Lantern.svg", "Lantern-dark.svg"))
    size = 'width="30.48mm" height="128.5mm"'
    assert size in light.read_text() and size in dark.read_text()  # Rack shows either in the same place
    assert not [p for p in EXAMPLES.rglob("*") if p.suffix.lower() in {".ttf", ".otf", ".woff", ".woff2"}]


@pytest.mark.host
@pytest.mark.gui
@rack_platforms()
def test_the_examples_over_racks_real_frames(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    user = tmp_path / "user"
    files = {"pythonpath/drawing_session.py": (HERE / "entries" / "drawing_session.py").read_text()}
    plugin = stage_plugin(stage_examples(make_hooks_bundle()), user, entry="drawing_session", files=files)
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"] += manifest_entries()
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    shots = tmp_path / "shots"
    shots.mkdir()
    result_path = tmp_path / "result.json"
    (plugin / "pythonpath" / "drawing_config.json").write_text(
        json.dumps({"result": str(result_path), "shots": str(shots)})
    )
    lantern = {"plugin": "RackPy", "model": "Lantern", "version": "2.0.0", "id": 2, "pos": [0, 0]}
    meter = {"plugin": "RackPy", "model": "Meter", "version": "2.0.0", "id": 3, "pos": [8, 0]}
    cable = {"id": 4, "outputModuleId": 2, "outputId": 0, "inputModuleId": 3, "inputId": 0, "color": "#e8a33d"}
    write_patch(
        user,
        [
            {**PROBE_MODULE, "pos": [30, 0]},
            {**lantern, "params": [{"id": 0, "value": 1.0}]},
            {**meter, "params": [{"id": 0, "value": 1.0}]},
        ],
        [cable],
    )
    run = run_rack_windowed(rack_host, user)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    result = json.loads(result_path.read_text())
    assert "error" not in result, result["error"]

    # Panels: the light SVG, then the dark one when Rack prefers dark panels.
    assert near(result["light_panel"], (0xE9, 0xE3, 0xD5)), result["light_panel"]
    assert near(result["dark_panel"], (0x1D, 0x20, 0x26)), result["dark_panel"]
    # A room at 25%: the panel dims to about a quarter; the lamp and the meter's bar stay bright.
    assert result["dim_panel"][0] / result["light_panel"][0] == pytest.approx(0.25, abs=0.04), result["dim_panel"]
    assert result["lamp_bright"][0] > 200 and abs(result["dim_lamp"][0] - result["lamp_bright"][0]) <= 12
    assert (
        result["bar_pixels"] > 200
        and abs(result["dim_bar_pixels"] - result["bar_pixels"]) <= result["bar_pixels"] // 20
    )
    circle = result["shapes_pixels"]["circle"]
    assert near(circle, (0xF2, 0xA6, 0x3B)) and result["dim_shapes_circle"][0] < circle[0] // 2  # layer 0 dims
    assert near(result["shapes_pixels"]["panel"], (0x2E, 0x36, 0x44))
    assert result["shapes_pixels"]["triangle_edge"][0] > 150  # the red stroke
    # Text from Rack's font; none while the font is missing, and no error raised; back after.
    assert result["text_pixels"] > 50 and result["missing_font_text_pixels"] == 0
    assert result["restored_text_pixels"] > 50 and result["raised_during_missing_font"] == 0
    # The cached scale renders once per change, not every frame.
    assert result["idle_renders"] == 0
    assert result["range_render_frames"] is not None and result["range_renders"] == 1
    # A new graphics context renders the cache again and reloads the font.
    assert result["recreated"]["renders"] >= 1 and result["recreated"]["text_pixels"] > 50
    assert result["deleted"] is True and result["undone_text_pixels"] > 50
    assert result["preview"][0] > 0

    update = os.environ.get("RACKPY_UPDATE_SCREENSHOTS") == "1"
    compared = {}
    for name in SCREENSHOTS:
        width, height = result["shots"][name]
        rgba = (shots / f"{name}.rgba").read_bytes()
        image = png(width, height, rgba if name == "meter-preview" else opaque(rgba))
        (tmp_path / f"{name}.png").write_bytes(image)
        reference = SHOTS / f"{name}.png"
        if update:
            SHOTS.mkdir(parents=True, exist_ok=True)
            reference.write_bytes(image)
            continue
        ref_width, ref_height, ref_rgba = unpng(reference.read_bytes())
        assert (ref_width, ref_height) == (width, height), name
        compared[name] = difference(unpng(image)[2], ref_rgba)
    print(f"screenshots against their references (mean channel difference, share of far pixels): {compared}")
    if update:
        pytest.fail(f"rewrote {SHOTS}; review the images, then run again without RACKPY_UPDATE_SCREENSHOTS")
    for name, (mean, far) in compared.items():
        assert mean <= 2.0 and far <= 0.005, (name, mean, far, tmp_path / f"{name}.png")


@pytest.mark.host
@rack_platforms()
@pytest.mark.parametrize("project", PROJECTS, ids=lambda p: p.name)
def test_each_example_loads_as_its_own_plugin(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path, project: Path
) -> None:
    manifest = json.loads((project / "plugin.json").read_text())
    user = tmp_path / "user"
    plugins = user / PLUGINS
    plugins.mkdir(parents=True)
    plugin_tool("build", project, "--bundle", make_bundle(), "--dest", plugins)
    slug = manifest["slug"]
    modules: list[dict[str, object]] = [
        {
            "plugin": slug,
            "model": e["slug"],
            "version": manifest["version"],
            "id": 2 + i,
            "pos": [12 * i, 0],
            "params": [],
        }
        for i, e in enumerate(manifest["modules"])
    ]
    write_patch(user, modules, [])
    for launch in (1, 2):
        run = run_rack(rack_host, user, settle=1.5)
        assert_clean_exit(run)
        assert run.lines(f"Loaded plugin {slug} {manifest['version']}"), run.tail()
        assert not run.lines("RackPy host initialization failed") and not run.lines("Traceback"), run.tail()
        saved = saved_patch(user)
        expected = [(slug, e["slug"]) for e in manifest["modules"]]
        assert [(m["plugin"], m["model"]) for m in saved["modules"]] == expected, launch
