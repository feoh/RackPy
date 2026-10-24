# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-15B: Rack's per-module patch storage directories."""

import pathlib
import threading
from collections.abc import Callable
from typing import Any

import _rackpy_host
import pytest

from rackpy import raw
from rackpy.errors import ContextError, OwnershipError, ThreadError
from rackpy.raw.engine import Module

HOOKS = getattr(_rackpy_host, "_testing", None)
LIVE = _rackpy_host.bound_context_generation() is not None


def test_patch_storage_needs_a_sealed_module_and_rack() -> None:
    def early(m: Module) -> None:
        m.id = 5
        with pytest.raises(OwnershipError, match="after the constructor"):
            m.get_patch_storage_directory()

    m = Module.create(early)
    try:
        calls: list[Callable[[], object]] = [
            m.get_patch_storage_directory,
            m.create_patch_storage_directory,
            m.patch_storage,
        ]
        if not LIVE:
            for call in calls:
                with pytest.raises(ContextError, match="live bound"):
                    call()
            return
        errors: list[BaseException] = []

        def worker() -> None:
            for call in calls:
                try:
                    call()
                except BaseException as error:
                    errors.append(error)

        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()
        assert len(errors) == 3 and all(isinstance(e, ThreadError) for e in errors)
    finally:
        m.close()


def test_rack_needs_an_engine_id() -> None:
    if not LIVE:
        return
    m = Module.create()
    try:
        with pytest.raises(raw.Exception, match="valid ID"):
            m.get_patch_storage_directory()
    finally:
        m.close()


def test_engine_owned_storage_lives_in_the_autosave_folder() -> None:
    if not LIVE or HOOKS is None:
        return  # the production start-up run has neither the hooks nor a patch manager
    m = Module.create(lambda m: m.config(0, 0, 0))
    module_id = HOOKS.engine_adopt(m)
    raw_path = m.get_patch_storage_directory()
    path = m.patch_storage(create=False)
    try:
        assert isinstance(raw_path, str) and isinstance(path, pathlib.Path)
        assert path == pathlib.Path(raw_path) and path.parts[-2:] == ("modules", str(module_id))
        assert not path.exists()
        created = m.create_patch_storage_directory()
        assert created == raw_path and path.is_dir()
        (path / "sample.txt").write_text("kept")
        assert m.patch_storage() == path  # creating again is harmless
    finally:
        HOOKS.engine_delete(m)
    # Rack, not RackPy, garbage-collects storage on save: undo may need it back.
    assert (path / "sample.txt").read_text() == "kept"


def test_the_facade_reports_what_rack_ignores() -> None:
    if not LIVE or HOOKS is None:
        return
    m = Module.create(lambda m: m.config(0, 0, 0))
    HOOKS.engine_adopt(m)
    path = m.patch_storage(create=False)
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("a file where the directory should be")
        # Rack's createPatchStorageDirectory ignores the failure and returns the path.
        assert m.create_patch_storage_directory() == str(path)
        with pytest.raises(OSError, match="could not create the patch storage directory"):
            m.patch_storage()
        assert path.read_text() == "a file where the directory should be"
    finally:
        path.unlink(missing_ok=True)
        HOOKS.engine_delete(m)


def test_runnable_snippet() -> None:
    snippet = pathlib.Path(__file__).resolve().parents[4] / "docs" / "snippets" / "patch_storage.py"
    scope: dict[str, Any] = {}
    exec(compile(snippet.read_text(), str(snippet), "exec"), scope)
    if not LIVE or HOOKS is None:
        return  # defining the functions is all start-up can do
    m = Module.create(lambda m: m.config(0, 0, 0))
    HOOKS.engine_adopt(m)
    try:
        assert scope["load_table"](m) == []
        scope["save_table"](m, [0.5, -1.0])
        assert scope["load_table"](m) == [0.5, -1.0]
    finally:
        HOOKS.engine_delete(m)
