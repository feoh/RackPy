# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-30C: rack::settings, rack::SharedMutex/SharedLock and rack::WeakPtr in Rack's live UI."""

import math
import threading
import time
from collections.abc import Callable, Iterator
from typing import Any

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.raw import app
from rackpy.raw import settings as rset
from rackpy.raw import string as rstring
from rackpy.raw import widget as w
from rackpy.raw.math import Vec
from rackpy.raw.nanovg import NVGcolor
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
            "cable_labels",
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
    paths = S.recent_patch_paths
    assert isinstance(paths, list)
    paths.append("/c.vcv")
    assert S.recent_patch_paths == ["/a.vcv", "/b.vcv"]
    S.cable_labels = ["original"]
    labels = S.cable_labels
    assert isinstance(labels, list)
    labels.append("copy")
    assert S.cable_labels == ["original"]
    colors = S.cable_colors
    assert isinstance(colors, list)
    colors.clear()
    assert len(S.cable_colors) > 1
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


def plain(value: object) -> object:
    """A comparable form of a setting's value, from Python's property or from the native fixture (NaN equals NaN)."""
    if isinstance(value, Vec):
        return (plain(value.x), plain(value.y))
    if isinstance(value, NVGcolor):
        return tuple(plain(c) for c in value.rgba)
    if isinstance(value, (rset.KnobMode, rset.BrowserSort)):
        return int(value)
    if isinstance(value, rset.ModuleInfo):
        return ("ModuleInfo", value.enabled, value.favorite, value.added, plain(value.last_added))
    if isinstance(value, rset.PluginWhitelist):
        return ("PluginWhitelist", value.subscribed, tuple(sorted(value.module_slugs)))
    if isinstance(value, float) and math.isnan(value):
        return "nan"
    if isinstance(value, dict):
        return {k: plain(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [plain(v) for v in value]
    return value


def differing(a: dict[str, object], b: dict[str, object]) -> list[str]:
    """The keys whose values differ: names only, so that a failure never prints the login token."""
    return sorted(k for k in a.keys() | b.keys() if a.get(k) != b.get(k))


def rack_now() -> dict[str, object]:
    """Rack's settings globals now, read by the native fixture, in plain form."""
    return {name: plain(value) for name, value in HOOKS.rack_settings().items()}


def flip(current: bool) -> bool:
    return not current


def other(*candidates: Any) -> Callable[[Any], Any]:
    """The first candidate that differs from the current value (floats exact in float32)."""
    return lambda current: next(c for c in candidates if plain(c) != plain(current))


def module_info(added: int) -> rset.ModuleInfo:
    info = rset.ModuleInfo()
    info.favorite, info.added, info.last_added = True, added, 1.5e9
    return info


def plugin_whitelist() -> rset.PluginWhitelist:
    whitelist = rset.PluginWhitelist()
    whitelist.module_slugs = {"RP31E-Module"}
    return whitelist


# Every rack::settings variable: its Python property, the C++ global and a distinct, valid new value given
# the current one. headless and is_plugin are read-only.
SETTINGS: list[tuple[str, str, Callable[[Any], Any] | None]] = [
    ("settings_path", "settingsPath", lambda v: v + ".rp31e"),
    ("dev_mode", "devMode", flip),
    ("headless", "headless", None),
    ("is_plugin", "isPlugin", None),
    ("restart", "restart", flip),
    ("language", "language", lambda v: next(x for x in rstring.get_languages() if x != v)),
    ("safe_mode", "safeMode", flip),
    ("token", "token", lambda v: v + "rp31e"),
    ("window_maximized", "windowMaximized", flip),
    ("window_size", "windowSize", lambda v: Vec(v.x + 16, v.y + 8)),
    ("window_pos", "windowPos", other(Vec(13, 29), Vec(17, 31))),
    ("invert_zoom", "invertZoom", flip),
    ("mouse_wheel_zoom", "mouseWheelZoom", flip),
    ("pixel_ratio", "pixelRatio", other(2.0, 1.5)),
    ("ui_theme", "uiTheme", other("light", "hcdark")),
    ("cable_opacity", "cableOpacity", other(0.75, 0.25)),
    ("cable_tension", "cableTension", other(0.25, 0.75)),
    ("rack_brightness", "rackBrightness", other(0.5, 0.75)),
    ("spotlight_brightness", "spotlightBrightness", other(0.5, 0.75)),
    ("spotlight_radius", "spotlightRadius", other(150.0, 200.0)),
    ("halo_brightness", "haloBrightness", other(0.5, 0.75)),
    ("allow_cursor_lock", "allowCursorLock", flip),
    ("knob_mode", "knobMode", other(*rset.KnobMode.__members__.values())),
    ("knob_scroll", "knobScroll", flip),
    ("knob_linear_sensitivity", "knobLinearSensitivity", other(2.0**-10, 2.0**-9)),
    ("knob_scroll_sensitivity", "knobScrollSensitivity", other(2.0**-9, 2.0**-10)),
    ("sample_rate", "sampleRate", other(48000.0, 96000.0)),
    ("thread_count", "threadCount", other(2, 1)),
    ("tooltips", "tooltips", flip),
    ("cpu_meter", "cpuMeter", flip),
    ("lock_modules", "lockModules", flip),
    ("squeeze_modules", "squeezeModules", flip),
    ("prefer_dark_panels", "preferDarkPanels", flip),
    ("frame_rate_limit", "frameRateLimit", other(30.0, 120.0)),
    ("autosave_interval", "autosaveInterval", other(30.0, 60.0)),
    ("skip_load_on_launch", "skipLoadOnLaunch", flip),
    ("last_patch_directory", "lastPatchDirectory", lambda v: v + "/rp31e-patches"),
    ("last_selection_directory", "lastSelectionDirectory", lambda v: v + "/rp31e-selections"),
    ("recent_patch_paths", "recentPatchPaths", lambda v: ["/tmp/rp31e.vcv", *v]),
    ("cable_auto_rotate", "cableAutoRotate", flip),
    ("cable_colors", "cableColors", lambda v: [raw.nanovg.rgb(17, 34, 51 + i) for i in range(max(len(v), 1))]),
    ("cable_labels", "cableLabels", lambda v: [f"rp31e {i}" for i in range(max(len(v), 1))]),
    ("auto_check_updates", "autoCheckUpdates", flip),
    ("verify_https_certs", "verifyHttpsCerts", flip),
    ("show_tips_on_launch", "showTipsOnLaunch", flip),
    ("tip_index", "tipIndex", lambda v: v + 1),
    ("browser_sort", "browserSort", other(*rset.BrowserSort.__members__.values())),
    ("browser_zoom", "browserZoom", other(0.5, 1.0)),
    ("plugin_settings", "pluginSettingsJ", lambda v: {**(v or {}), "RP31E": {"n": 1}}),
    ("module_infos", "moduleInfos", lambda v: {**v, "RP31E-Plugin": {"RP31E-Module": module_info(7)}}),
    ("module_whitelist", "moduleWhitelist", lambda v: {**v, "RP31E-Plugin": plugin_whitelist()}),
]


@pytest.fixture
def rack_globals() -> Iterator[dict[str, object]]:
    """Rack's settings globals as they were (plain), restored natively afterwards, whatever the test did.

    restart and safe_mode matter most: left set, Rack would run again after quitting, or delete its autosave
    as its patch manager goes."""
    saved = HOOKS.rack_settings()
    before = {name: plain(value) for name, value in saved.items()}
    yield before
    for name, value in saved.items():
        if name not in {"headless", "isPlugin"}:
            HOOKS.set_rack_setting(name, value)
    assert differing(before, rack_now()) == []


def native_value(value: object) -> object:
    return int(value) if isinstance(value, (rset.KnobMode, rset.BrowserSort)) else value


def test_each_setting_is_its_rack_global(rack_globals: dict[str, object]) -> None:
    """Each property writes, and reads, its own rack::settings global and no other (so swapping two fails)."""
    properties = {n for n in dir(rset.Settings) if isinstance(getattr(rset.Settings, n), property)}
    assert {py for py, _, _ in SETTINGS} == properties
    assert {cpp for _, cpp, _ in SETTINGS} == set(rack_globals)
    before = rack_globals
    python_before = {py: plain(getattr(S, py)) for py, _, _ in SETTINGS}
    assert all(python_before[py] == before[cpp] for py, cpp, _ in SETTINGS)
    for py, cpp, change in SETTINGS:
        if change is None:  # read-only
            with pytest.raises(AttributeError):
                setattr(S, py, python_before[py])
            continue
        original = HOOKS.rack_settings()[cpp]
        value = change(getattr(S, py))
        assert plain(value) != before[cpp], py
        try:
            setattr(S, py, value)  # Python writes; the fixture reads Rack's globals
            after = rack_now()
            reached = after[cpp] == plain(value)
            assert reached, f"{py} did not write {cpp}"
            assert differing(before, after) == [cpp], f"{py} wrote another global"
        finally:
            HOOKS.set_rack_setting(cpp, original)
        try:
            HOOKS.set_rack_setting(cpp, native_value(value))  # the fixture writes Rack's global; Python reads
            seen = {name: plain(getattr(S, name)) for name, _, _ in SETTINGS}
            read = seen[py] == plain(value)
            assert read, f"{py} did not read {cpp}"
            assert differing(python_before, seen) == [py], f"{cpp} reached another property"
        finally:
            HOOKS.set_rack_setting(cpp, original)
        assert differing(before, rack_now()) == []


def test_knob_modes_and_browser_sorts_by_name_and_value(rack_globals: dict[str, object]) -> None:
    native = HOOKS.rack_settings_enumerators()  # Rack's enumerators, from C++
    assert native == {
        "KNOB_MODE_LINEAR": 0,
        "KNOB_MODE_SCALED_LINEAR": 1,
        "KNOB_MODE_ROTARY_ABSOLUTE": 2,
        "KNOB_MODE_ROTARY_RELATIVE": 3,
        "BROWSER_SORT_UPDATED": 0,
        "BROWSER_SORT_LAST_USED": 1,
        "BROWSER_SORT_MOST_USED": 2,
        "BROWSER_SORT_BRAND": 3,
        "BROWSER_SORT_NAME": 4,
        "BROWSER_SORT_RANDOM": 5,
    }
    enums = (
        ("KNOB_MODE_", rset.KnobMode, "knob_mode", "knobMode"),
        ("BROWSER_SORT_", rset.BrowserSort, "browser_sort", "browserSort"),
    )
    for prefix, enum, field, cpp in enums:
        assert sorted(enum.__members__) == sorted(n for n in native if n.startswith(prefix))
        for name, value in native.items():
            if not name.startswith(prefix):
                continue
            member = getattr(enum, name)
            assert member is getattr(rset, name) and member.name == name and int(member) == value
            setattr(S, field, member)
            assert HOOKS.rack_settings()[cpp] == value  # Rack's global holds this enumerator
            HOOKS.set_rack_setting(cpp, value)
            assert getattr(S, field) == member and getattr(S, field).name == name


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
    view.favorite, view.last_added = False, 1.5e9  # a Unix time
    native = HOOKS.rack_settings()["moduleInfos"]["RackPy"]["RuntimeProbe"]
    assert not native.favorite and native.last_added == 1.5e9 and native.added == 4


def test_plugin_whitelist(rack_globals: dict[str, object]) -> None:
    whitelist = rset.PluginWhitelist()
    assert not whitelist.subscribed and whitelist.module_slugs == set()
    whitelist.module_slugs = {"RuntimeProbe"}
    slugs = whitelist.module_slugs
    assert isinstance(slugs, set)
    slugs.add("CopyOnly")
    assert whitelist.module_slugs == {"RuntimeProbe"}
    assert isinstance(S.module_whitelist, dict)
    S.module_whitelist = {"RackPy": whitelist}
    assert rset.is_module_whitelisted("RackPy", "RuntimeProbe") and not rset.is_module_whitelisted("RackPy", "Other")
    assert rset.is_module_whitelisted("OtherPlugin", "Any")  # Rack's: no entry, every module
    native = HOOKS.rack_settings()["moduleWhitelist"]["RackPy"]
    assert not native.subscribed and native.module_slugs == {"RuntimeProbe"}
    whitelist.subscribed = True  # subscribed to the whole plugin
    S.module_whitelist = {"RackPy": whitelist}
    assert (
        rset.is_module_whitelisted("RackPy", "Other") and HOOKS.rack_settings()["moduleWhitelist"]["RackPy"].subscribed
    )
    slugs = S.module_whitelist["RackPy"].module_slugs
    slugs.add("Changed")  # a set copy: Rack's is unchanged
    assert S.module_whitelist["RackPy"].module_slugs == {"RuntimeProbe"}


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
    lock = raw.SharedLock(m)
    assert held(lock)
    lock.release()
    assert not held(lock)
    assert m.try_lock()
    lock.release()  # a second release must not unlock an unrelated acquisition
    blocked: list[bool] = []
    probe3 = threading.Thread(target=lambda: blocked.append(m.try_lock()))
    probe3.start()
    probe3.join()
    assert blocked == [False]
    m.unlock()


class ReadLock:
    """A TMutex of Python's own: Rack's SharedLock<TMutex> needs only lock_shared() and unlock_shared()."""

    def __init__(self) -> None:
        self.calls: list[str] = []
        self.fail: set[str] = set()

    def lock_shared(self) -> None:
        self.calls.append("lock")
        if "lock" in self.fail:
            raise RuntimeError("lock_shared failed")

    def unlock_shared(self) -> None:
        self.calls.append("unlock")
        if "unlock" in self.fail:
            raise RuntimeError("unlock_shared failed")


def test_shared_lock_takes_any_object_with_lock_shared() -> None:
    m = ReadLock()
    with raw.SharedLock(m) as lock:
        assert held(lock) and lock.m is m and m.calls == ["lock"]
    assert not held(lock) and m.calls == ["lock", "unlock"]
    lock.release()  # once, as Rack's destructor runs once
    assert m.calls == ["lock", "unlock"]
    for wrong in (object(), 3, type("OnlyLock", (), {"lock_shared": lambda self: None})()):
        with pytest.raises(TypeError, match="lock_shared"):
            raw.SharedLock(wrong)  # type: ignore[arg-type]
    with pytest.raises(TypeError, match="lock_shared"):
        raw.SharedLock(type("NotCallable", (), {"lock_shared": 1, "unlock_shared": 2})())


def test_shared_lock_propagates_errors_from_lock_shared_and_unlock_shared() -> None:
    m = ReadLock()
    m.fail = {"lock"}
    with pytest.raises(RuntimeError, match="lock_shared failed"):
        raw.SharedLock(m)  # no lock is made
    m.fail = {"unlock"}
    lock = raw.SharedLock(m)
    with pytest.raises(RuntimeError, match="unlock_shared failed"):
        lock.release()
    assert held(lock)  # unlock_shared failed, so m still counts as held
    m.fail = set()
    lock.release()
    assert not held(lock) and m.calls == ["lock", "lock", "unlock", "unlock"]
    m.fail = {"unlock"}
    with pytest.raises(RuntimeError, match="unlock_shared failed"), raw.SharedLock(m) as failing:
        pass
    m.fail = set()
    failing.release()


def test_shared_lock_dropped_while_held(monkeypatch: pytest.MonkeyPatch) -> None:
    unraisable: list[str] = []
    monkeypatch.setattr("sys.unraisablehook", lambda u: unraisable.append(f"{u.exc_type.__name__}: {u.exc_value}"))
    m = ReadLock()
    lock = raw.SharedLock(m)
    del lock  # on the thread that locked it: unlock_shared, as C++ RAII would
    assert m.calls == ["lock", "unlock"] and not unraisable
    m.fail = {"unlock"}
    lock = raw.SharedLock(m)
    del lock
    assert unraisable == ["RuntimeError: unlock_shared failed"]
    m.fail, m.calls = set(), []
    box = [raw.SharedLock(m)]
    dropper = threading.Thread(target=box.clear)  # dropped on a thread that did not lock it: no Python runs
    dropper.start()
    dropper.join()
    assert m.calls == ["lock"] and len(unraisable) == 2 and "did not lock it" in unraisable[1]


def test_shared_lock_on_a_python_subclass_of_shared_mutex() -> None:
    class Counting(raw.SharedMutex):
        def __init__(self) -> None:
            super().__init__()
            self.reads = 0

        def lock_shared(self) -> None:
            self.reads += 1
            super().lock_shared()

    c = Counting()
    with raw.SharedLock(c) as lock:
        writer: list[bool] = []
        probe = threading.Thread(target=lambda: writer.append(c.try_lock()))
        probe.start()
        probe.join()
        assert c.reads == 1 and held(lock) and writer == [False]  # Rack's rwlock, held for reading
        with pytest.raises(ValueError, match="holds the mutex already"):
            raw.SharedLock(c)  # RackPy's misuse refusal still applies
    assert not held(lock) and c.try_lock()
    c.unlock()


def test_shared_lock_waits_on_a_python_lock_without_holding_a_native_one() -> None:
    gate = threading.Lock()
    gate.acquire()

    class Gated:
        def lock_shared(self) -> None:
            gate.acquire()  # blocks; Python's lock lets the GIL go while it waits

        def unlock_shared(self) -> None:
            gate.release()

    def opener() -> None:
        time.sleep(0.05)  # needs the GIL to run
        gate.release()

    t = threading.Thread(target=opener)
    t.start()
    lock = raw.SharedLock(Gated())  # waits for opener
    t.join(2)
    assert not t.is_alive() and held(lock)
    lock.release()
    assert not gate.locked()


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


def weak_handle(widget: w.Widget) -> raw.WeakHandle | None:
    handle: raw.WeakHandle | None = widget.weak_handle  # a call, so type checkers do not narrow it across changes
    return handle


def test_weak_handle_is_made_by_weak_ptr() -> None:
    # Rack's WeakPtr<T>::set runs WeakHandle(void*) for an object without a handle (weakptr.hpp:80-83) and deletes
    # the handle with its last WeakPtr (weakptr.hpp:66-77); that is how RackPy reaches the constructor.
    widget = w.Widget()
    p = raw.WeakPtr()
    assert weak_handle(widget) is None
    p.set(widget)  # WeakHandle(widget): ptr is the widget, count 1
    handle = weak_handle(widget)
    assert handle is not None and handle.ptr is widget and handle.count == 2  # p and the view
    assert p.weak_handle is not None and p.weak_handle.ptr is widget
    del handle
    assert widget.get_weak_count() == 1
    p.set(None)  # the last WeakPtr: Rack deletes the handle and clears the widget's
    assert weak_handle(widget) is None and widget.get_weak_count() == 0
    again = raw.WeakPtr(widget)  # a new handle for the same widget
    assert again.get() is widget and widget.get_weak_count() == 1
    with pytest.raises(TypeError):
        raw.WeakHandle(widget)  # type: ignore[call-arg]
    with pytest.raises(RuntimeError, match="Widget"):
        raw.WeakPtr(object())  # WeakPtr<T> needs a WeakBase, and Widget is Rack's only one
    widget.close()
    assert again.get() is None
