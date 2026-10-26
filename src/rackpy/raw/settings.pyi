# SPDX-License-Identifier: GPL-3.0-or-later
"""rack::settings: Rack's settings."""

import enum
from typing import Any, final

from rackpy.raw.math import Vec
from rackpy.raw.nanovg import NVGcolor

class KnobMode(enum.IntEnum):
    KNOB_MODE_LINEAR = 0
    KNOB_MODE_SCALED_LINEAR = 1
    KNOB_MODE_ROTARY_ABSOLUTE = 2
    KNOB_MODE_ROTARY_RELATIVE = 3

class BrowserSort(enum.IntEnum):
    BROWSER_SORT_UPDATED = 0
    BROWSER_SORT_LAST_USED = 1
    BROWSER_SORT_MOST_USED = 2
    BROWSER_SORT_BRAND = 3
    BROWSER_SORT_NAME = 4
    BROWSER_SORT_RANDOM = 5

KNOB_MODE_LINEAR: KnobMode
KNOB_MODE_SCALED_LINEAR: KnobMode
KNOB_MODE_ROTARY_ABSOLUTE: KnobMode
KNOB_MODE_ROTARY_RELATIVE: KnobMode
BROWSER_SORT_UPDATED: BrowserSort
BROWSER_SORT_LAST_USED: BrowserSort
BROWSER_SORT_MOST_USED: BrowserSort
BROWSER_SORT_BRAND: BrowserSort
BROWSER_SORT_NAME: BrowserSort
BROWSER_SORT_RANDOM: BrowserSort

class ModuleInfo:
    enabled: bool
    favorite: bool
    added: int
    last_added: float
    def __init__(self) -> None: ...

@final
class ModuleInfoView:
    enabled: bool
    favorite: bool
    added: int
    last_added: float

class PluginWhitelist:
    subscribed: bool
    module_slugs: set[str]
    def __init__(self) -> None: ...

@final
class Settings:
    """Rack's settings, one property each. Main thread."""

    settings_path: str
    dev_mode: bool
    @property
    def headless(self) -> bool: ...
    @property
    def is_plugin(self) -> bool: ...
    restart: bool
    language: str
    safe_mode: bool
    token: str
    window_maximized: bool
    window_size: Vec
    window_pos: Vec
    invert_zoom: bool
    mouse_wheel_zoom: bool
    pixel_ratio: float
    ui_theme: str
    cable_opacity: float
    cable_tension: float
    rack_brightness: float
    spotlight_brightness: float
    spotlight_radius: float
    halo_brightness: float
    allow_cursor_lock: bool
    knob_mode: KnobMode
    knob_scroll: bool
    knob_linear_sensitivity: float
    knob_scroll_sensitivity: float
    sample_rate: float
    thread_count: int
    tooltips: bool
    cpu_meter: bool
    lock_modules: bool
    squeeze_modules: bool
    prefer_dark_panels: bool
    frame_rate_limit: float
    autosave_interval: float
    skip_load_on_launch: bool
    last_patch_directory: str
    last_selection_directory: str
    recent_patch_paths: list[str]
    cable_colors: list[NVGcolor]
    cable_labels: list[str]
    cable_auto_rotate: bool
    auto_check_updates: bool
    verify_https_certs: bool
    show_tips_on_launch: bool
    tip_index: int
    browser_sort: BrowserSort
    browser_zoom: float
    plugin_settings: Any
    module_infos: dict[str, dict[str, ModuleInfo]]
    module_whitelist: dict[str, PluginWhitelist]

settings: Settings

def get_module_info(plugin_slug: str, module_slug: str) -> ModuleInfoView | None: ...
def is_module_whitelisted(plugin_slug: str, module_slug: str) -> bool: ...
def reset_cables() -> None: ...
