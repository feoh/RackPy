# RP-22F validation (Linux x86_64)

Date 2026-10-26/27, branch `rp22f-driver-lab`, based on `origin/main`
`4ef82b2` (RP-22D, its review fix, the trampoline re-entrancy fix and its
integration evidence). GCC 16.2.1, bundled CPython 3.14, pinned Rack
Free/SDK 2.6.6; pinned inputs as in [rp-27a-validation.md](rp-27a-validation.md).
This worktree's own `build/` only; every host run is serialized with
`flock /tmp/rackpy-host-tests.lock` and keeps its log under
`build/validation/`. No audio or MIDI device is opened. No push.

RP-22F (`tk-rackpy-22f-laboratory-python-paths-for-audio-and-65ccf3`) gives
the 33 `driver`-strategy virtuals that RP-22D moved to `planned` their
laboratory Python path, next to RP-29's native production path. They move
back to `implemented`, **not `verified`**. Decisions:
[rp-22-slices.md](rp-22-slices.md); explanation:
[laboratory audio and MIDI drivers](../docs/explanation/callbacks.md#laboratory-audio-and-midi-drivers).

## Design (supervisor-approved, 2026-10-26)

- Python-owned `rackpy.laboratory.AudioDriver`, `AudioDevice`, `MidiDriver`,
  `MidiInputDevice`, the existing `MidiOutputDevice` (now with a configured
  `name` and `get_name`), and `MidiInput`/`MidiInputQueue`. Creation and
  `connect` need `rackpy.laboratory.enable()`. None is ever registered with
  Rack (`rackpy.raw.audio.add_driver`/`midi.add_driver` refuse them), so only
  Python-made ports reach them, on the main thread.
- `driver.connect(port)` points a Python-made port's public `driver`/`device`
  fields at the laboratory driver. Rack's own `audio::Port`, `midi::Port`,
  `Input`, `Output`, `InputDevice` and `app::Midi*Choice` code then calls the
  overrides synchronously. `super()` is the native configured answer through
  a qualified (non-virtual) call. Errors reach the Python caller through
  Rack's frames; Rack's port setters keep the GIL for a laboratory device so
  that they do.
- **One record without a Rack caller:** `audio::Driver::getDefaultDeviceId`
  is called by Rack 2.6.6 only from `audio::Port::setDriverId`
  (`src/audio.cpp:93-123`, the call at line 116), which finds drivers by
  registered ID. `connect` repeats setDriverId's calls (leave the device,
  take the driver, ask the default device, Rack's `setDeviceId`), as the
  Bench repeats Rack's engine. Registering with Rack's public `addDriver` is
  not an alternative: Rack has no public removal, so it would be irreversible
  and expose the driver to Rack's UI and driver threads. The record's notes
  and the docs say this one virtual is reached through RackPy's reproduction,
  not Rack's own function. The call order is pinned against Rack's source
  (`test_racks_audio_port_calls_a_python_driver_and_device`: default device,
  then the driver's and the device's subscribe); no registered native driver
  was used to compare, because none can be added reversibly.
- `MidiInput` is a `rackpy.raw.midi.Input` subclass whose constructor sets a
  native per-instance mark after checking enablement (as RP-22E). On a Rack
  thread Python is refused and the message is queued for the UI thread, as
  for any Python input. `MidiInputQueue` is a separate native class (the raw
  `InputQueue` is final) whose Python base is `rackpy.raw.midi.Input`;
  `isinstance(q, rackpy.raw.midi.InputQueue)` is False (documented).
- Teardown runs no Python: a released port (deleted, Rack's context closing)
  unsubscribes through the driver natively, never handed to Python, and
  leaves every laboratory device; removing or deleting a device, or deleting
  a driver, makes every Python port leave it and point nowhere.
- A driver holds strong references to its devices (GC traverse/clear), and a
  `subscribe` override must return one of them or `None`, else
  `OwnershipError`.

## Records (33, `planned` → `implemented`)

| Record | Laboratory override | Rack's caller in the tests |
| --- | --- | --- |
| `audio::Device::getName()` | `AudioDevice.get_name` | `audio::Port::toJson`; `Port::getDeviceName` via the driver's super() |
| `audio::Device::getNumInputs()` | `AudioDevice.get_num_inputs` | `Port::getNumInputs`; `Port::getDeviceNumInputs` via super() |
| `audio::Device::getNumOutputs()` | `AudioDevice.get_num_outputs` | `Port::getNumOutputs`; `Port::getDeviceNumOutputs` via super() |
| `audio::Device::getSampleRates()` | `AudioDevice.get_sample_rates` | `Port::getSampleRates` |
| `audio::Device::getSampleRate()` | `AudioDevice.get_sample_rate` | `Port::getSampleRate`, `Port::toJson` |
| `audio::Device::setSampleRate(float)` | `AudioDevice.set_sample_rate` | `Port::setSampleRate` |
| `audio::Device::getBlockSizes()` | `AudioDevice.get_block_sizes` | `Port::getBlockSizes` |
| `audio::Device::getBlockSize()` | `AudioDevice.get_block_size` | `Port::getBlockSize`, `Port::toJson` |
| `audio::Device::setBlockSize(int)` | `AudioDevice.set_block_size` | `Port::setBlockSize` (error test too) |
| `audio::Device::subscribe(Port *)` | `AudioDevice.subscribe` | `Port::setDeviceId` → the driver's subscribe super() |
| `audio::Device::unsubscribe(Port *)` | `AudioDevice.unsubscribe` | `Port::setDeviceId` (old device) → the driver's unsubscribe super() |
| `audio::Driver::getName()` | `AudioDriver.get_name` | `Port::getDriverName` |
| `audio::Driver::getDeviceIds()` | `AudioDriver.get_device_ids` | `Port::getDeviceIds` |
| `audio::Driver::getDefaultDeviceId()` | `AudioDriver.get_default_device_id` | **RackPy's** repetition of `Port::setDriverId` in `AudioDriver.connect` |
| `audio::Driver::getDeviceName(int)` | `AudioDriver.get_device_name` | `Port::getDeviceName` |
| `audio::Driver::getDeviceNumInputs(int)` | `AudioDriver.get_device_num_inputs` | `Port::getDeviceNumInputs` |
| `audio::Driver::getDeviceNumOutputs(int)` | `AudioDriver.get_device_num_outputs` | `Port::getDeviceNumOutputs` |
| `audio::Driver::subscribe(int, Port *)` | `AudioDriver.subscribe` | `Port::setDeviceId` |
| `audio::Driver::unsubscribe(int, Port *)` | `AudioDriver.unsubscribe` | `Port::setDeviceId` (old device) |
| `midi::Device::getName()` | `MidiInputDevice.get_name`, `MidiOutputDevice.get_name` | `midi::Port::toJson`, `app::MidiDeviceChoice::step`, `Input/Output::getDeviceName` via super() |
| `midi::Driver::getName()` | `MidiDriver.get_name` | `app::MidiDriverChoice::step` |
| `midi::Driver::getInputDeviceIds()` | `MidiDriver.get_input_device_ids` | `Input::getDeviceIds` |
| `midi::Driver::getDefaultInputDeviceId()` | `MidiDriver.get_default_input_device_id` | `Input::getDefaultDeviceId` (and connect's setDriverId repetition) |
| `midi::Driver::getInputDeviceName(int)` | `MidiDriver.get_input_device_name` | `Input::getDeviceName` |
| `midi::Driver::subscribeInput(int, Input *)` | `MidiDriver.subscribe_input` | `Input::setDeviceId` |
| `midi::Driver::unsubscribeInput(int, Input *)` | `MidiDriver.unsubscribe_input` | `Input::setDeviceId` (old device) |
| `midi::Driver::getOutputDeviceIds()` | `MidiDriver.get_output_device_ids` | `Output::getDeviceIds` (error test too) |
| `midi::Driver::getDefaultOutputDeviceId()` | `MidiDriver.get_default_output_device_id` | `Output::getDefaultDeviceId` (and connect) |
| `midi::Driver::getOutputDeviceName(int)` | `MidiDriver.get_output_device_name` | `Output::getDeviceName` |
| `midi::Driver::subscribeOutput(int, Output *)` | `MidiDriver.subscribe_output` | `Output::setDeviceId` |
| `midi::Driver::unsubscribeOutput(int, Output *)` | `MidiDriver.unsubscribe_output` | `Output::setDeviceId` (old device) |
| `midi::Input::onMessage(const Message &)` | `MidiInput.on_message` | `midi::InputDevice::onMessage` (a `MidiInputDevice.push_message`; Rack's Loopback driver from `Output::sendMessage`) |
| `midi::InputQueue::onMessage(const Message &)` | `MidiInputQueue.on_message` | `midi::InputDevice::onMessage` (`MidiInputDevice.push_message`) |

Tests: `tests/laboratory/in_rack/test_audio_devices.py` and
`test_midi_devices.py` (run by `tests/laboratory/test_laboratory.py` in the
production build at startup and in the test-hooks build with a live
context), plus the live-context shutdown variants of
`tests/laboratory/test_races.py`. Each virtual is reached by Rack's caller
on the main thread; `super()`; errors through Rack's frames (including an
`OwnershipError` for a foreign device returned from `subscribe`); refusal
while the laboratory is disabled and of registration with Rack; ordinary and
constructor-bypassing inputs keep the queued path; test-hooks probes call
every driver/device virtual (24 audio, 15 MIDI calls) and both `onMessage`s
from a Rack-context thread, and require exactly that many
`refused_rack_thread` increments, no Python call and the configured answers;
a port and device view retained from a callback point nowhere / raise
`StaleHandleError` after the device is removed; deleting a port, a device or
a driver runs no Python and leaves Rack's ports pointing nowhere; overrides
that leave subscriptions inconsistent leave no dangling port; a
driver/device cycle is collected. At context close (live-context shutdown
scenario, both the retaining and the releasing variant), `rackpy._bootstrap.stop`
checks that the connected laboratory audio port and MIDI input are stale,
that Rack's own fields point nowhere (`_testing.port_targets`), that the
devices list no subscriber and that no override ran
(`LAB SHUTDOWN laboratory drivers disconnected without Python`).

Supervisor-approved test-hooks additions (compiled only with
`RACKPY_TEST_HOOKS`): `laboratory_audio_on_rack_thread`,
`laboratory_midi_on_rack_thread`, `midi_input_on_rack_thread` and
`port_targets`.

## Found and fixed while validating

- **Messages were not owned copies.** The first targeted live run failed
  `test_a_laboratory_input_receives_messages_synchronously` (62 passed,
  1 failed, 1 skipped): Rack's `InputDevice::onMessage` passes the caller's
  own message on when its frame is set, and pybind11 then handed Python the
  caller's registered `Message` object. Laboratory inputs, input queues and
  (also through `OutputDeviceView.send_message`) the laboratory output
  device now pass an explicit copy; the tests check identity.
- A laboratory input's mark is read on driver threads, so it is atomic, and
  its released flag is read only where Python may run.
- The shutdown scenario reused the name `stop` (its worker's `Event`) for the
  wrapped bootstrap hook; strict typing caught it before any run.
- The first docs build had three warnings: two `(label)=` page links (MyST
  page links need heading anchors, as RP-22C found) and a `:meth:` role to an
  undocumented native method; fixed, then 0 warnings.

## Commands and outcomes

| Command | Result |
| --- | --- |
| `uv sync --frozen` | Pass, first command in this worktree |
| `bin/rackpy-dev build --werror --test-hooks` | Pass, CTest 6/6, on the final sources (`build-final.log`) |
| `uv run pytest -m unit` | Pass: 100 passed, 201 deselected |
| `uv run pytest -m "native and not host"` | Pass: 32 passed, 269 deselected, 254.73 s (`native-tests.log`) |
| `prek run --all-files` | Pass, all hooks (first run: one ruff-format change in `tests/coverage/test_callback_paths.py`, fixed) |
| `bin/rackpy-dev typecheck` | Pass, strict mypy |
| `bin/planning-check` | Pass: 45 tasks, 114 dependencies, acyclic |
| `uv run bin/generate_bindings.py check` | Pass, after `generate` (generated files never hand-edited) |
| `uv run bin/check_coverage.py check --without-built-package` | Before (base manifest): absent 2, excluded_internal 111, implemented 2890, planned 54, unreviewed 1. After: absent 2, excluded_internal 111, implemented 2923, planned 21, unreviewed 1. Zero verified before and after; the denominator is unchanged. The 21 planned are RP-28F's UI virtuals |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` (serial) | Pass, 0 warnings (`docs-build.log`) |
| `uv run pytest -m "docs and not host"` | Pass: 15 passed, 286 deselected, 151.63 s (`docs-tests.log`) |
| `flock … uv run pytest -m "host and not dist" tests/laboratory/test_laboratory.py -ra --basetemp=build/validation/lab1` | **Failed** (the copy bug above): 1 passed, 1 failed, 445.24 s (`lab1.log`) |
| `flock … uv run pytest -m "host and not dist" tests/laboratory/test_laboratory.py tests/laboratory/test_races.py -k "laboratory_in or live_rack or shutdown" -ra --basetemp=build/validation/lab2` | Pass: 6 passed, 4 deselected, 451.32 s (`lab2.log`). In Rack: production 43 passed, 18 skipped (exactly the live-context tests); live context 63 passed, 1 skipped (the no-context test). All four shutdown variants passed; both live-context ones logged the disconnect marker |

## RP-22 closing validation: the full host suite

RP-22F is RP-22's last open child, so the supervisor moved RP-22's closing
full-suite run here (the RP-22D full run had failed on a shutdown-scenario
setup error, corrected and re-run there as a targeted run only).

`CMAKE_BUILD_PARALLEL_LEVEL=8 flock /tmp/rackpy-host-tests.lock timeout 5400 uv run pytest -m "host and not dist" -ra --basetemp=build/validation/host-full -o tmp_path_retention_policy=all`,
on commit `c500e86` (the tree of this commit, apart from this section of
this file): **123 passed, 178 deselected, 0 failed, 0 skipped, 758.71 s
(12m39s), exit 0** (`host-full.log`, `host-full.exit`). The `-ra` summary
reports nothing (no skip, xfail or xpass). The outer 5400-second timeout was
not reached.

- In Rack, the laboratory suites gave the same counts as the targeted run:
  production 43 passed, 18 skipped (exactly the live-context tests); live
  context 63 passed, 1 skipped (the no-context test); each
  `in_rack_result.json` reports exit 0.
- The four shutdown variants passed: the two retaining variants log the
  designed refusal to finalize (`RackPy did not finalize the interpreter: 3
  Python reference(s) are still held by native objects`), the two releasing
  variants `Py_FinalizeEx returned 0`, and both live-context variants log
  `LAB SHUTDOWN laboratory drivers disconnected without Python`.
- The intentional deadlock probe
  `tests/integration/test_lifecycle_probe.py::test_unsafe_worker_deadlocks_and_is_caught_by_the_watchdog`
  passed: its own 30-second watchdog kills the deliberately deadlocked Rack
  (`returncode is None`), after `onReset` took the engine lock without the
  GIL. That kill is the test's expected outcome, not a failure; no other
  watchdog fired.
- The serial docs build was repeated on the committed state: 0 warnings
  (`docs-build-final.log`).

## Remaining gates

Linux x86_64 only. macOS, Windows, Rack Pro, real audio/MIDI hardware and
actual driver-thread scheduling are not exercised; the Rack-thread probes
establish the refusal guard, not real-time behaviour. A laboratory path
proves only the documented offline semantics. RP-28F's 21 UI virtuals
remain full-release blockers; no record is `verified`. No distribution
build, release or push.
