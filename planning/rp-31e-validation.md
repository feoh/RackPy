# RP-31E validation (Linux x86_64)

Branch `rp31e` from fc4aa6d. The 2026-10-27 checkpoint had a failing PUT test
and incomplete validation; the subsequent work and final checks are recorded
below. Earlier logs are in `build/validation/rp31e-*.log`, which git ignores.

Records: the 65 `services_io` entries of
[rp-31-audit/evidence.json](rp-31-audit/evidence.json) with weak or missing
tests or missing docs, plus the settings records of
[P1-4](rp-31-audit/qualitative-adapters-transitive.md) (the existence-only
`token`, `uiTheme`, `lastPatchDirectory`, `tooltips`, `cpuMeter`,
`frameRateLimit`, `tipIndex`). The manifest's `tests`, `docs` and `notes`
fields changed for 101 records; no status or other field changed (checked by
diffing against fc4aa6d). Nothing was promoted.

## Commands run (on the working tree, before the commit)

| Command | Result |
| --- | --- |
| `uv sync --frozen` | ok |
| `bin/rackpy-dev build --werror --test-hooks` | builds, including the new `native/testing/testing.cpp` fixture; CTest 100% of 6 pass |
| `bin/rackpy-dev typecheck` | strict mypy: no issues (269 source files, and each `bin/` script) |
| `prek run --all-files` | all hooks pass |
| `uv run pytest -m unit` | 106 passed, 203 deselected |
| `bin/planning-check` | 45 tasks, 114 dependencies, acyclic |
| `uv run bin/generate_bindings.py check` | generated files match the manifest |
| `uv run bin/check_coverage.py check --without-built-package` | absent 2, excluded_internal 111, implemented 2945 (unchanged) |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` (serial, fresh output) | 0 warnings |
| `uv run pytest -q -m "docs and not host"` | 15 passed |
| `flock /tmp/rackpy-host-tests.lock uv run pytest -q -rA tests/services/test_settings.py tests/services/test_utilities.py tests/services/test_system.py tests/io/test_audio.py tests/io/test_virtual.py "tests/io/test_midi.py::test_midi_in_live_rack"` | **5 passed, 1 failed** (260 s): `test_system_and_network_in_rack` |

At that checkpoint, built-package introspection, mutation checks and a host
run on the committed tree had not yet run; the later checks are below.

## The failure: Rack's `METHOD_PUT` sends POST

`test_json_requests` now sends `put_json(url, {"y": "z"})` and
`request_json(METHOD_PUT, ...)` to the loopback server, which echoes the
method. The server saw `POST`. Rack 2.6.6 (`src/network.cpp:128-148`) sets
`CURLOPT_UPLOAD` for `METHOD_PUT` and then `CURLOPT_POSTFIELDS` for the body,
and curl then sends POST. So with a body, `METHOD_PUT` is indistinguishable
from `METHOD_POST`, and the facade's `put_json` does not PUT. Without a body,
curl uploads from its default read function, stdin. The raw binding preserves Rack's behavior and documents it. The higher-level
`put_json` now raises `NotImplementedError` instead of making an unintended
POST (or reading stdin); the host test checks both and asserts Rack's PUT enum
value, so swapping it with POST cannot pass just because their requests match. On the updated tree the
werror test-hooks build and 6 CTests, targeted services/audio/MIDI host tests
and built-package introspection passed (9 outer tests). The tests used temporary
fixture overrides to select the existing test-hooks build; the fixture was
restored. Three independent mutation experiments on the updated tree swapped
`invert_zoom`/`mouse_wheel_zoom`, two `KnobMode` bindings, and the DEBUG/INFO
logger enum bindings. The respective actual-Rack settings tests failed at
`test_each_setting_is_its_rack_global` and
`test_knob_modes_and_browser_sorts_by_name_and_value`; the logger host test
failed (its earlier existing log assertion), and its log also showed the new
level markers labeled `info` for DEBUG and `debug` for INFO, contradicting the
new level assertions. Each experiment restored its source and rebuilt the
unmutated binary.

## Per-record changes

| Records | Test change | Docs change |
| --- | --- | --- |
| all 49 `variable:rack::settings::*` | new `test_each_setting_is_its_rack_global`: for each property, writes a distinct valid value through Python and reads every global through the new native hook `_testing.rack_settings()` (C++ names, `rack::settings::X` read directly), then writes through `_testing.set_rack_setting` and reads every Python property; each step must change exactly that one global or property, so swapped macros fail, bools included. Every value is restored at once, and by the `rack_globals` fixture. Citations of `test_settings_read_and_write` dropped where it never names the setting | `services.md#services-settings`: the name rule, and the effects of `restart`, `safe_mode`, `dev_mode`, `verify_https_certs`, `window_*`, `settings_path`, `sample_rate` |
| `KnobMode`, `BrowserSort` and their 10 enumerators | new `test_knob_modes_and_browser_sorts_by_name_and_value`: names and values against Rack's C++ enumerators (`_testing.rack_settings_enumerators`), each written through the setting to Rack's global | new `#services-settings-values` |
| `ModuleInfo` and its 4 fields | `test_module_infos` reads `favorite` and `last_added` back from Rack's `moduleInfos` natively | `#services-settings-values` |
| `PluginWhitelist` and its 2 fields, `isModuleWhitelisted` | new `test_plugin_whitelist`: Rack's three cases, both fields read back natively, `module_slugs` a set copy | `#services-settings-values` |
| `Level::DEBUG/INFO/WARN/FATAL_LEVEL`, macros `DEBUG/INFO/WARN/FATAL` | new `test_every_log_level`; the outer test finds each message in `log.txt` under Rack's label (`debug`, `info`, `warn`, `fatal`) with the Python caller | `services.md#services-logger` names the levels and labels |
| `METHOD_PUT` | `test_json_requests` PUTs (fails, above) | `Method` listed in `#services-network` |
| `audio::Device` record, `Port::getDevice/getDeviceId/getDriverId/getDriverName` | `test_python_ports_on_a_python_device` reads them on a port holding a Python device | the getters added to `audio.md#audio-ports` |
| `audio::Device::onStopStream` | `test_python_ports_on_a_python_device` calls `stop_stream` and sees `on_stop_stream` | already documented |
| `~Driver` (audio, MIDI) | new `test_rack_deletes_python_drivers_as_it_quits`: an `on_destroy` callback finds both `PythonDriver` handles stale; the outer test reads it in Rack's log after "Destroying MIDI", "Destroying audio", "Destroying plugins" (passed) | disposition in `audio.md#audio-drivers`, `midi.md#midi-drivers`, and the records' notes |
| `~Device` (audio, MIDI) | cite `test_active_stream_module_deletion_and_hotplug` and the quit test (audio), `test_a_python_midi_driver` (MIDI) | `alive` after deletion documented; disposition in notes |
| `midi::Input::inputDevice`, `Port::getDevice/getDeviceId/getDriverId`, `InputDevice`/`OutputDevice` `subscribe`/`unsubscribe` | new `test_port_getters_and_device_subscriptions`: getters across two loopback devices; a subscribed input hears the device until it unsubscribes; `OwnershipError` for subscriptions `subscribe` did not make (passed) | `midi.md#midi-ports`, `#midi-devices` |

## Final checks after restoring mutations

- Werror test-hooks build and 6 CTests passed; the restored binary was rebuilt
  after the mutations.
- Actual Rack services, settings, utilities, audio, virtual audio/MIDI, MIDI
  and built-package introspection: 9 outer tests passed, serialized with the
  host lock. `pytest -m unit`: 106 passed; `pytest -m docs`: 17 passed.
- Actual Rack values/reference suite on the test-hooks build passed. A second
  test expecting a *production* build failed because the temporary fixture
  override incorrectly supplied that same test-hooks build, so its expected
  test-hooks skips were absent; this was a fixture-selection error, not a
  production-build validation. The override was restored.
- `prek run --all-files`, `rackpy-dev typecheck`, generator and source
  coverage checks, and Sphinx with warnings as errors passed. Built-package
  names were checked separately by the introspection host test.

No records were promoted to verified. This is Linux x86_64 evidence only;
macOS and Windows and a fresh production build were not tested here.
