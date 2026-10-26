# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-30C: rack::settings, rack::SharedMutex/SharedLock and rack::WeakPtr in Rack's live UI."""

import math
import threading
import time
from collections.abc import Iterator

import pytest

from rackpy import raw
from rackpy.raw import app
from rackpy.raw import settings as rset
from rackpy.raw import widget as w
from rackpy.raw.math import Vec
from rackpy.raw.window import Svg

S = rset.settings
LIGHT = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="60" height="380"><rect width="60" height="380" fill="#ddd"/></svg>'
)
DARK = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="60" height="380"><rect width="60" height="380" fill="#222"/></svg>'
)


@pytest.fixture
def restore() -> Iterator[None]:
    saved = {
        name: getattr(S, name)
        for name in (
            "prefer_dark_panels",
            "rack_brightness",
            "window_size",
            "cable_colors",
            "recent_patch_paths",
            "knob_mode",
            "plugin_settings",
            "module_infos",
            "thread_count",
            "language",
            "browser_sort",
        )
    }
    yield
    for name, value in saved.items():
        setattr(S, name, value)


def test_settings_read_and_write(restore: None) -> None:
    assert isinstance(S.headless, bool)
    with pytest.raises(AttributeError):
        S.headless = False  # type: ignore[misc]  # Rack's code assumes it from start-up
    assert isinstance(S.is_plugin, bool) and S.settings_path.endswith("settings.json")
    S.rack_brightness = 0.25
    assert S.rack_brightness == 0.25
    with pytest.raises(ValueError, match="finite"):
        S.rack_brightness = math.nan
    S.window_size = Vec(800, 600)
    assert (S.window_size.x, S.window_size.y) == (800, 600)
    S.cable_colors = [raw.nanovg.rgb(255, 0, 0)]
    assert len(S.cable_colors) == 1 and S.cable_colors[0].r == 1.0
    rset.reset_cables()  # Rack's default colours
    assert len(S.cable_colors) > 1 and len(S.cable_labels) == len(S.cable_colors)
    S.recent_patch_paths = ["/a.vcv", "/b.vcv"]
    assert S.recent_patch_paths == ["/a.vcv", "/b.vcv"]
    S.knob_mode = rset.KnobMode.KNOB_MODE_ROTARY_ABSOLUTE
    assert S.knob_mode == rset.KNOB_MODE_ROTARY_ABSOLUTE
    S.browser_sort = rset.BrowserSort.BROWSER_SORT_NAME
    assert S.browser_sort == rset.BROWSER_SORT_NAME
    S.plugin_settings = {"RackPy": {"x": 1}}
    assert S.plugin_settings == {"RackPy": {"x": 1}}
    with pytest.raises(ValueError, match="at least one"):
        S.thread_count = 0
    S.language = "en"
    assert S.language == "en"
    for name in ("token", "ui_theme", "last_patch_directory", "tooltips", "cpu_meter", "frame_rate_limit", "tip_index"):
        assert getattr(S, name) is not None


def test_module_infos(restore: None) -> None:
    info = rset.ModuleInfo()
    assert info.enabled and not info.favorite and info.added == 0 and math.isnan(info.last_added)
    info.favorite, info.added = True, 3
    S.module_infos = {"RackPy": {"RuntimeProbe": info}}
    view = rset.get_module_info("RackPy", "RuntimeProbe")
    assert view is not None and view.favorite and view.added == 3
    view.added = 4  # writes Rack's
    assert S.module_infos["RackPy"]["RuntimeProbe"].added == 4
    assert rset.get_module_info("RackPy", "NoSuchModule") is None
    whitelist = rset.PluginWhitelist()
    whitelist.module_slugs = {"RuntimeProbe"}
    assert isinstance(rset.is_module_whitelisted("RackPy", "RuntimeProbe"), bool)
    assert isinstance(S.module_whitelist, dict)


def test_prefer_dark_panels_rerenders_themed_widgets(restore: None) -> None:
    light, dark = Svg(), Svg()
    light.load_string(LIGHT)
    dark.load_string(DARK)
    panel = app.ThemedSvgPanel()
    S.prefer_dark_panels = False
    panel.set_background(light, dark)
    panel.step()
    assert panel.svg is light
    panel.fb.dirty = False
    S.prefer_dark_panels = True  # as Rack's View menu does
    panel.step()  # Rack's ThemedSvgPanel switches in step()
    assert panel.svg is dark and panel.fb.dirty  # its framebuffer re-renders
    panel.close()


def held(lock: raw.SharedLock) -> bool:
    return lock.held  # a call, so that type checkers do not narrow it across the with block


def test_shared_mutex() -> None:
    m = raw.SharedMutex()
    m.lock()
    with pytest.raises(ValueError, match="deadlock"):
        m.lock()  # Rack's rwlock would fail its assertion
    got: list[bool] = []
    other = threading.Thread(target=lambda: got.append(m.try_lock()))
    other.start()
    other.join()
    assert got == [False]
    m.unlock()
    with pytest.raises(ValueError, match="does not hold"):
        m.unlock()
    with m:
        flag: list[bool] = []
        probe = threading.Thread(target=lambda: flag.append(m.try_lock_shared()))
        probe.start()
        probe.join()
        assert flag == [False]  # a writer excludes readers
    readers: list[str] = []
    m.lock_shared()

    def read() -> None:
        m.lock_shared()  # readers share it
        readers.append("in")
        m.unlock_shared()

    t = threading.Thread(target=read)
    t.start()
    t.join(2)
    assert readers == ["in"]
    m.unlock_shared()
    with pytest.raises(ValueError, match="shared"):
        m.unlock_shared()
    with raw.SharedLock(m) as lock:
        assert held(lock) and lock.m is m
        writer: list[bool] = []
        probe2 = threading.Thread(target=lambda: writer.append(m.try_lock()))
        probe2.start()
        probe2.join()
        assert writer == [False]  # a reader excludes writers
    assert not held(lock) and m.try_lock()
    m.unlock()


def test_waiting_releases_the_gil() -> None:
    m = raw.SharedMutex()
    held = threading.Event()

    def holder() -> None:
        m.lock()
        held.set()
        time.sleep(0.05)  # needs the GIL to wake and let go
        m.unlock()

    t = threading.Thread(target=holder)
    t.start()
    held.wait(2)
    m.lock()  # waits for the holder, which runs Python meanwhile
    m.unlock()
    t.join()


def test_weak_pointers() -> None:
    widget = w.Widget()
    assert widget.get_weak_count() == 0 and widget.weak_handle is None
    p = raw.WeakPtr(widget)
    assert p.get() is widget and bool(p) and widget.get_weak_count() == 1
    handle = p.weak_handle
    assert handle is not None and handle.ptr is widget and handle.count == 2  # the handle view holds one too
    q = p.__copy__()
    assert q.get() is widget
    widget.close()
    assert p.get() is None and not p and q.get() is None and handle.ptr is None
    empty = raw.WeakPtr()
    assert empty.get() is None and not empty and empty.weak_handle is None
    host = raw.context_get()
    assert host is not None and host.scene is not None
    scene_ptr = raw.WeakPtr(host.scene)  # Rack's own widgets too
    assert scene_ptr.get() == host.scene and host.scene.get_weak_count() >= 1
    scene_ptr.set(None)
    assert scene_ptr.get() is None
