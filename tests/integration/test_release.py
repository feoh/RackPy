# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-39: the parts of bin/rackpy-release and bin/rackpy-plugin pack that make artifacts reproducible and clean.

The whole build and the clean-machine install run in CI's release-candidate job
(bin/rackpy-release build, then verify) and in planning/rp-39-validation.md; these
check the pieces on small trees.
"""

import importlib.machinery
import importlib.util
import json
import os
import tarfile
import types
from pathlib import Path

import pytest

from tests.support import BIN

pytestmark = pytest.mark.unit


def script(name: str) -> types.ModuleType:
    loader = importlib.machinery.SourceFileLoader(name.replace("-", "_"), str(BIN / name))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


@pytest.fixture(scope="module")
def release() -> types.ModuleType:
    return script("rackpy-release")


@pytest.fixture(scope="module")
def plugin_tool() -> types.ModuleType:
    return script("rackpy-plugin")


def make_tree(root: Path, mtime: int) -> Path:
    (root / "python" / "lib").mkdir(parents=True)
    (root / "pythonpath" / "__pycache__").mkdir(parents=True)
    (root / "pythonpath" / "__pycache__" / "m.cpython-314.pyc").write_bytes(str(mtime).encode())
    (root / "plugin.json").write_text('{"slug": "X", "version": "2.0.0", "license": "GPL-3.0-or-later"}')
    (root / "plugin.so").write_bytes(b"\x7fELF fake")
    (root / "plugin.so").chmod(0o750)
    (root / "python" / "lib" / "libpython3.14.so.1.0").write_bytes(b"lib")
    (root / "python" / "lib" / "libpython3.14.so").symlink_to("libpython3.14.so.1.0")
    for path in root.rglob("*"):
        os.utime(path, (mtime, mtime), follow_symlinks=False)
    return root


def test_packages_of_one_tree_are_identical_whatever_the_files_dates_and_bytecode(
    plugin_tool: types.ModuleType, tmp_path: Path
) -> None:
    first, second = tmp_path / "a.vcvplugin", tmp_path / "b.vcvplugin"
    plugin_tool.pack_tree(make_tree(tmp_path / "one" / "X", 1_000_000), first, 1_700_000_000)
    plugin_tool.pack_tree(make_tree(tmp_path / "two" / "X", 2_000_000), second, 1_700_000_000)
    assert first.read_bytes() == second.read_bytes()
    with tarfile.open(first, "r:zst") as tf:
        members = tf.getmembers()
    assert [m.name for m in members] == [
        "X",
        "X/plugin.json",
        "X/plugin.so",
        "X/python",
        "X/python/lib",
        "X/python/lib/libpython3.14.so",
        "X/python/lib/libpython3.14.so.1.0",
        "X/pythonpath",
    ]
    assert {(m.uid, m.gid, m.uname, m.gname, m.mtime) for m in members} == {(0, 0, "", "", 1_700_000_000)}
    modes = {m.name: m.mode for m in members}
    assert modes["X/plugin.so"] == 0o755 and modes["X/plugin.json"] == 0o644
    assert next(m for m in members if m.issym()).linkname == "libpython3.14.so.1.0"


def test_files_naming_the_build_machine_are_found(release: types.ModuleType, tmp_path: Path) -> None:
    tree = make_tree(tmp_path / "tree", 1_000_000)
    assert release.machine_paths(tree) == []
    (tree / "python" / "leak.so").write_bytes(b"\0" + str(Path.home() / ".cache" / "x.h").encode() + b"\0")
    (tree / "notes.txt").write_text(f"built in {release.REPO}")
    assert sorted(p.name for p, _ in release.machine_paths(tree)) == ["leak.so", "notes.txt"]


def test_compare_names_the_members_that_differ(
    release: types.ModuleType, plugin_tool: types.ModuleType, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    for name, payload in [("a", b"lib"), ("b", b"rebuilt")]:
        tree = make_tree(tmp_path / f"tree-{name}" / "X", 1_000_000)
        (tree / "python" / "lib" / "libpython3.14.so.1.0").write_bytes(payload)
        out = tmp_path / name
        out.mkdir()
        plugin_tool.pack_tree(tree, out / "X-2.0.0-lin-x64.vcvplugin", 1_700_000_000)
        (out / "notes.txt").write_text("same")
    release.compare(tmp_path / "a", tmp_path / "b")
    report = json.loads(capsys.readouterr().out)
    assert report == {
        "X-2.0.0-lin-x64.vcvplugin": {"differing_members": ["X/python/lib/libpython3.14.so.1.0"]},
        "notes.txt": "identical",
    }


def test_the_license_pin_is_the_same_build_as_the_runtime() -> None:
    inputs = json.loads((BIN.parent / "packaging" / "inputs.json").read_text())
    runtime, licenses = inputs["python_runtime"]["3.14"], inputs["python_licenses"]["3.14"]
    tag = f"cpython-{runtime['version']}+{runtime['release']}-x86_64-unknown-linux-gnu-"
    assert runtime["file"].startswith(tag) and licenses["file"].startswith(tag)
    assert licenses["file"].endswith("-pgo+lto-full.tar.zst")
    assert len(licenses["sha256"]) == 64


def test_pack_names_the_package_as_rack_does_and_refuses_a_misnamed_folder(
    plugin_tool: types.ModuleType, tmp_path: Path
) -> None:
    tree = make_tree(tmp_path / "MySlug", 1_000_000)
    (tree / "plugin.json").write_text('{"slug": "MySlug", "version": "2.1.0"}')
    plugin_tool.pack(tree, dest=tmp_path / "out", source_date_epoch=5)
    with tarfile.open(tmp_path / "out" / "MySlug-2.1.0-lin-x64.vcvplugin", "r:zst") as tf:
        assert {m.mtime for m in tf.getmembers()} == {5}
    (tree / "plugin.json").write_text('{"slug": "Other", "version": "2.1.0"}')
    with pytest.raises(SystemExit, match="named after its slug"):
        plugin_tool.pack(tree, dest=tmp_path / "out")


def test_the_file_manifest_covers_every_file_or_names_the_ones_it_cannot(
    release: types.ModuleType, tmp_path: Path
) -> None:
    tree = make_tree(tmp_path / "X", 1_000_000)
    (tree / "licenses").mkdir()
    (tree / "kernels").mkdir()
    (tree / "kernels" / "k.so").write_bytes(b"kernel")
    assert release.file_manifest(tree, {"commit": "c"}) == []
    files = json.loads((tree / "licenses" / "FILES.json").read_text())["files"]
    assert files["kernels/k.so"]["components"] == ["plugin", "rack-sdk-headers"]
    assert files["python/lib/libpython3.14.so.1.0"]["components"] == ["cpython", "cpython-libraries"]
    assert files["python/lib/libpython3.14.so"] == {"components": ["cpython"], "symlink": "libpython3.14.so.1.0"}
    assert not any("__pycache__" in name for name in files)  # as rackpy-plugin pack leaves them out
    (tree / "stray.bin").write_bytes(b"?")
    assert release.file_manifest(tree, {"commit": "c"}) == ["X/stray.bin"]
