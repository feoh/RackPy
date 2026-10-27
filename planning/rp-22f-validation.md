# RP-22F validation (Linux x86_64)

Date 2026-10-26/27, branch `rp22f-driver-lab`. GCC 16.2.1, bundled CPython
3.14, pinned Rack Free/SDK 2.6.6; pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md).

**Commits.** RP-22F was written and first validated on `4ef82b2` (commits
`c500e86`/`749f00e`, history below). The supervisor then rebased it onto
`465b22d` (`getDeviceDetail` recorded as declared but never defined) and
`9d954d1` (RP-28F: its 21 UI records implemented as calls on Rack's
instances); the RP-22F commit became `ebb9438`, a conflict-resolved replay
whose generated files, generator and `callbacks.md` were resolved by the
supervisor, followed by `fa0f391` (docs only). An independent review of
`ebb9438` requested changes; the review fixes are the commit
"RP-22F review: delivery guard, subscription and teardown order" on top of
`fa0f391`. **Every check below was rerun on that final tree** (the section
"Commands and outcomes"); the earlier runs are kept as history.
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

## Review fixes (on `fa0f391`)

The independent review of `ebb9438` requested changes; all four are fixed.

- **P1, synchronous delivery inside Rack's loop.** A laboratory input's
  `on_message` runs inside Rack's `InputDevice::onMessage`
  (`src/midi.cpp:52-53`, `for (Input* input : subscribed)`), so an override
  that changed a subscriber set, or dropped its own port or a driver, could
  invalidate Rack's iterator or free what it uses.
  - `bindings::SynchronousMidiDelivery` (RAII, thread-local depth, nesting
    counts) wraps every synchronous Python `on_message` of a laboratory
    `MidiInput` or `MidiInputQueue`, including the bound method's release.
  - While it is active, everything that changes a subscriber set raises
    `LaboratoryError` naming the rule: `Port.set_device_id`,
    `set_driver_id`, `from_json`, `Input.reset`/`Output.reset`, making a
    MIDI port (`Input()`, `InputQueue()`, `Output()`,
    `laboratory.MidiInputQueue()`; Rack's constructor subscribes it),
    `connect` (laboratory driver and output device), every
    `subscribe_*`/`unsubscribe_*` and device `subscribe`/`unsubscribe` (Rack's
    drivers' views and the laboratory's), and `remove_*_device` (laboratory
    and `PythonDriver`).
  - Deferral instead of refusal where nothing can be refused (dropping a
    reference): at the outermost entry the guard keeps every live
    laboratory MIDI driver and device alive, and each entry keeps its
    delivered port alive (`runtime::PyRef`s). A driver's destruction would
    unsubscribe its ports, possibly Rack's current element, and a device's
    would clear the iterated set, so neither may happen mid-loop; deleting
    another, non-current port only erases a non-current set element, which
    leaves Rack's iterator valid.
  - Release: the guard's own end is still inside Rack's loop, so the kept
    objects go only when no delivery is in progress, at the end of the
    Python call that started Rack's loop (`MidiDeliveryCall` in
    `push_message` of both input devices, `Output.send_message`,
    `OutputDevice.send_message`, `InputDevice.on_message`). That is the
    first point known to be after Rack's loop, so objects dropped in
    `on_message` are gone when the caller gets control back. Rack's own
    main-thread deliveries (its computer keyboard driver) have no such
    call, so a UI-step hook releases what they kept; context closing and
    finalization release the rest. The release loop tolerates destructors
    that deliver again.
- **P2, a `subscribe` override removing its device.** The native
  `AudioDriver::subscribe` returns `find(id) == d ? d : nullptr` after the
  possibly-Python `d->subscribe(port)` (IDs are never reused).
- **P2, teardown order.** `AudioDriver` and `MidiDriver` now have
  destructors that first detach their ports (`Unlisted*Driver::detach()`,
  also called by the base destructors) and then release their devices with
  `auto drop = std::move(devices)`, so a device's `__del__` or weakref
  callback that queries a port finds no driver instead of a half-destroyed
  map.
- **P1, this record.** Rewritten for the committed tree: counts, commits and
  gates below.

New in-Rack tests (live context; they need MIDI or audio ports, so they skip
in the production startup run, which `test_laboratory.py` requires):

- `test_midi_devices.py::test_subscriptions_cannot_change_during_a_synchronous_delivery`:
  through a laboratory `push_message` and through Rack's Loopback driver
  (`Output.send_message`), 13 subscription-changing operations inside
  `on_message` each raise `LaboratoryError`; nothing changed afterwards and
  the same operations work once the call returned; an uncaught refusal
  reaches the Python caller through Rack's loop.
- `test_objects_dropped_during_a_delivery_go_after_it`: through both routes,
  `on_message` drops the last references to its own port and to a
  laboratory driver used by another port; both are alive for the rest of the
  delivery (checked after `gc.collect()` inside it), Rack's loop still
  reaches the second subscriber, and after the call returns both are
  collected and the other driver's port points nowhere.
- `test_a_delivery_that_raises_still_releases_what_it_kept`.
- `test_midi_driver_teardown_detaches_ports_before_its_devices_go`
  (weakref callback calling `get_device_ids`/`get_default_device_id`).
- `test_audio_devices.py::test_a_subscribe_override_that_removes_its_device_leaves_the_port_pointing_nowhere`
  and `test_audio_driver_teardown_detaches_ports_before_its_devices_go`
  (`__del__` calling `get_device_ids`/`get_driver_name`).

The manifest cites them on the 22 affected records (the two `onMessage`
records also describe the delivery rule); generated files regenerated.

**Not run: AddressSanitizer.** The plugin is not built with sanitizers
(only standalone native benchmarks are), and Rack loads the production
`plugin.so`; no ASan reproduction of the iteration hazard was run. The new
tests show refusal and deferral behaviour in actual Rack, not memory-safety
under a sanitizer.

## Commands and outcomes (final tree)

Run on the review-fix tree (the commit's content, apart from the full-suite
section of this file), after `uv sync --frozen`:

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks` | Pass, CTest 6/6 (`review-build.log`) |
| `uv run pytest -m unit` | Pass: 101 passed, 202 deselected |
| `uv run pytest -m "native and not host"` | Pass: 33 passed, 270 deselected, 211.06 s (`review-native.log`) |
| `prek run --all-files` | Pass, all hooks |
| `bin/rackpy-dev typecheck` | Pass: strict mypy, 266 source files plus the CLI checks |
| `bin/planning-check` | Pass: 45 tasks, 114 dependencies, acyclic |
| `uv run bin/generate_bindings.py check` | Pass, after `generate` |
| `uv run bin/check_coverage.py check --without-built-package` | Before RP-22F (`9d954d1`): absent 2, excluded_internal 111, implemented 2912, planned 33. After (`ebb9438`, `fa0f391` and this tree): absent 2, excluded_internal 111, implemented 2945, planned 0, unreviewed 0. Zero verified; the 3058-record denominator is unchanged |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` (serial) | Pass, 0 warnings (`review-docs-build.log`) |
| `uv run pytest -m "docs and not host"` | Pass: 15 passed, 288 deselected, 132.55 s |
| `flock … uv run pytest -m "host and not dist" tests/laboratory/test_laboratory.py -ra --basetemp=build/validation/review-lab1` | **Failed**, a test bug: 1 passed, 1 failed. `test_objects_dropped_during_a_delivery_go_after_it` kept a device view of the dropping port, and a view holds its port, so the port outlived the call. The test now takes the view from the other port. In Rack: production 43 passed, 24 skipped; live context 68 passed, 1 skipped, 1 failed |
| `flock … uv run pytest -m "host and not dist" tests/laboratory/test_laboratory.py tests/laboratory/test_races.py -ra --basetemp=build/validation/review-lab2` | Pass: 10 passed, 446.61 s. In Rack: production 43 passed, 24 skipped (exactly the tests needing the live context); live context 69 passed, 1 skipped (the no-context test). Shutdown variants: retaining ones refuse to finalize as designed, releasing ones `Py_FinalizeEx returned 0`, both live-context ones log `LAB SHUTDOWN laboratory drivers disconnected without Python` |

## RP-22 closing validation: the full host suite

RP-22F is RP-22's last open child, so the supervisor moved RP-22's closing
full-suite run here (the RP-22D full run had failed on a shutdown-scenario
setup error, corrected and re-run there as a targeted run only).

`CMAKE_BUILD_PARALLEL_LEVEL=8 flock /tmp/rackpy-host-tests.lock timeout 5400 uv run pytest -m "host and not dist" -ra --basetemp=build/validation/review-host-full -o tmp_path_retention_policy=all`,
on commit `5cce50c` (the review-fix commit before this section was added;
this file is the only difference): **123 passed, 180 deselected, 0 failed,
0 skipped, 744.17 s (12m24s), exit 0** (`review-host-full.log`,
`review-host-full.exit`). The `-ra` summary reports nothing (no skip,
xfail or xpass), no Rack log records a fatal signal, and the outer
5400-second timeout was not reached.

- In Rack, the laboratory suites: production 43 passed, 24 skipped
  (exactly the tests needing the live context); live context 69 passed,
  1 skipped (the no-context test); each `in_rack_result.json` reports exit 0.
- The four shutdown variants passed: the two retaining variants log the
  designed refusal to finalize (`RackPy did not finalize the interpreter`),
  the two releasing variants `Py_FinalizeEx returned 0`, and both
  live-context variants log
  `LAB SHUTDOWN laboratory drivers disconnected without Python`.
- The intentional deadlock probe
  `tests/integration/test_lifecycle_probe.py::test_unsafe_worker_deadlocks_and_is_caught_by_the_watchdog`
  passed: its own 30-second watchdog kills the deliberately deadlocked Rack
  (`returncode is None`), after `onReset` took the engine lock without the
  GIL. That kill is the test's expected outcome, not a failure; no other
  watchdog fired.

## History: the first validation, on `4ef82b2`

Before the rebase and the review, on `c500e86`: build, unit (100 passed),
native (32 passed), prek, typecheck, planning-check, generate check,
`check_coverage` (implemented 2923, planned 21, unreviewed 1: RP-28F's 21
were still planned then), sphinx (0 warnings), docs (15 passed); a first
targeted laboratory run failed on messages that were not owned copies
(fixed, below); the second passed (6 passed); the full host suite passed
(123 passed, 0 skipped, 758.71 s). That tree is not the committed one, so
none of it is claimed for it.

Found and fixed then:

- **Messages were not owned copies.** Rack's `InputDevice::onMessage`
  passes the caller's own message on when its frame is set, and pybind11
  then handed Python the caller's registered `Message` object. Laboratory
  inputs, input queues and the laboratory output device pass an explicit
  copy; the tests check identity.
- A laboratory input's mark is read on driver threads, so it is atomic, and
  its released flag is read only where Python may run.
- The shutdown scenario reused the name `stop` for the wrapped bootstrap
  hook; strict typing caught it before any run.
- Three docs warnings: two `(label)=` page links and a `:meth:` role to an
  undocumented native method.

## Remaining gates

Linux x86_64 only. macOS, Windows, Rack Pro, real audio/MIDI hardware and
actual driver-thread scheduling are not exercised; the Rack-thread probes
establish the refusal guard, not real-time behaviour, and no sanitizer run
covers the delivery guard. A laboratory path proves only the documented
offline semantics. No record is `verified`; with RP-28F's records
implemented, no record is `planned` or `unreviewed`, which is not a
full-coverage claim (`verified` needs review). A laboratory driver or device
whose last Python reference is dropped on a Python-started thread is
destroyed there, off the main thread (pre-existing, not changed here). No
distribution build, release or push.
