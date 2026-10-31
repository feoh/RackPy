# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38: the plugin's folder disappears while Rack runs it, as an update or uninstall underneath would do.

Rack 2 never unloads a plugin while it runs: it calls the plugin's destroy() at exit,
and RackPy keeps its host and Python runtime mapped after that (RTLD_NODELETE). What
can happen is the files going away. Then what is already loaded keeps working, what
needs a file fails where it is used, and Rack quits cleanly (the outer test checks).
"""

import importlib
import time
from pathlib import Path

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


def test_the_plugin_folder_removed_under_a_running_plugin(tmp_path: Path) -> None:
    plugin = assets.plugin()
    (plugin / "pythonpath" / "rp38_late.py").write_text("VALUE = 1\n")  # shipped, never imported before the move
    (plugin / "res").mkdir(exist_ok=True)
    (plugin / "res" / "rp38.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10"/>')
    module_id = HOOKS.engine_adopt(counting_module())
    moved = tmp_path / "moved-away"
    plugin.rename(moved)  # the folder is gone; open and mapped files stay valid
    try:
        assert not plugin.exists()
        importlib.invalidate_caches()
        with pytest.raises(ModuleNotFoundError):
            importlib.import_module("rp38_late")
        assert Svg.load(str(plugin / "res" / "rp38.svg")) is None  # Rack's loader reports it; no exception reaches Rack
        running = HOOKS.engine_handle(module_id)
        assert running is not None
        before = running.outputs[0].get_voltage()
        time.sleep(0.05)
        assert running.outputs[0].get_voltage() > before  # the kernel loaded before still runs
        added = HOOKS.engine_adopt(counting_module())  # new modules and kernels still work: nothing reads the folder
        fresh = HOOKS.engine_handle(added)
        assert fresh is not None
        time.sleep(0.05)
        assert fresh.outputs[0].get_voltage() > 0
        HOOKS.engine_delete(fresh)
    finally:
        moved.rename(plugin)  # Rack's later file reads (a patch save with this plugin's modules) find it again
    handle = HOOKS.engine_handle(module_id)
    assert handle is not None
    HOOKS.engine_delete(handle)
