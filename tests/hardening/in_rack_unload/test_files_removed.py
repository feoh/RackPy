# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38: the plugin's folder disappears while Rack runs it, as an update or uninstall underneath would do.

Rack 2 never unloads a plugin while it runs: it calls the plugin's destroy() at exit,
and RackPy keeps its host and Python runtime mapped after that (RTLD_NODELETE). What
can happen is the files going away. Then what is already loaded keeps working, what
needs a file fails where it is used, and Rack quits cleanly (the outer test checks).
"""

import importlib
import sys
import time
from collections.abc import Callable

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import assets
from rackpy.dsp.graph import Graph
from rackpy.raw.engine import Module
from rackpy.raw.window import Svg


def counting_module() -> Module:
    def configure(m: Module) -> None:
        m.config(0, 0, 1, 0)
        g = Graph()
        frames = g.delay(0.0)
        frames.feed(frames + 1)
        g.output(0, frames)
        m.set_kernel(g.compile())

    return Module.create(configure)


def rises(read: Callable[[], float], above: float, within: float = 5.0) -> bool:
    """Whether a voltage the engine writes goes above a value within some seconds (a slow CI machine's engine may
    not have run the module yet after a fixed short sleep, RP-42)."""
    deadline = time.monotonic() + within
    while time.monotonic() < deadline:
        if read() > above:
            return True
        time.sleep(0.02)
    return False


def test_the_plugin_folder_removed_under_a_running_plugin() -> None:
    plugin = assets.plugin()
    (plugin / "pythonpath" / "rp38_late.py").write_text("VALUE = 1\n")  # shipped, never imported before the move
    (plugin / "res").mkdir(exist_ok=True)
    (plugin / "res" / "rp38.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10"/>')
    module_id = HOOKS.engine_adopt(counting_module())
    moved = plugin.with_name(plugin.name + ".moved-away")  # beside it: a rename cannot cross drives (Windows)
    try:
        plugin.rename(moved)  # the folder is gone; open and mapped files stay valid
    except PermissionError:
        # Windows refuses to move a folder whose DLLs are loaded: the files cannot go away under Rack there.
        assert sys.platform == "win32"
        running = HOOKS.engine_handle(module_id)
        assert running is not None and rises(running.outputs[0].get_voltage, running.outputs[0].get_voltage())
        HOOKS.engine_delete(running)
        return
    try:
        assert not plugin.exists()
        importlib.invalidate_caches()
        with pytest.raises(ModuleNotFoundError):
            importlib.import_module("rp38_late")
        assert Svg.load(str(plugin / "res" / "rp38.svg")) is None  # Rack's loader reports it; no exception reaches Rack
        running = HOOKS.engine_handle(module_id)
        assert running is not None
        assert rises(running.outputs[0].get_voltage, running.outputs[0].get_voltage())  # the loaded kernel still runs
        added = HOOKS.engine_adopt(counting_module())  # new modules and kernels still work: nothing reads the folder
        fresh = HOOKS.engine_handle(added)
        assert fresh is not None
        assert rises(fresh.outputs[0].get_voltage, 0.0)
        HOOKS.engine_delete(fresh)
    finally:
        moved.rename(plugin)  # Rack's later file reads (a patch save with this plugin's modules) find it again
    handle = HOOKS.engine_handle(module_id)
    assert handle is not None
    HOOKS.engine_delete(handle)
