# RP-22D validation (Linux x86_64)

Date: 2026-10-26/27 UTC.

Base: `7d2c543` (`rp22d-closing`), pinned Rack Free/SDK 2.6.6,
bundled CPython 3.14, GCC 16.2.1. This worktree only; no push.
All host runs use `flock /tmp/rackpy-host-tests.lock`, bounded Rack subprocess
watchdogs and persistent output under `build/validation/`. Sphinx runs serially.

## Scope and owner/supervisor decisions

The generated callback inventory joins all 434 public virtual declarations
from `bin/callback-matrix` to the manifest without trimming the denominator.
It preserves overloads, destructors and the value virtual as distinct entries.
`tests/coverage/test_callback_paths.py` checks every row and the release blockers.
No native production/laboratory implementation or public Python API changed;
existing stubs remain accurate. Only generated mapping/registration metadata
changes, through `bin/generate_bindings.py generate`.

The supervisor supplied live RP-22D and parent RP-22 descriptions/comments
(`/tmp/rp22d-live-task.txt`); task/workflow claims remain with the supervisor.
The audit found native-only callback coverage overstated as implemented:

- Approved correction: 33 driver/device native-configured/queued-only virtuals
  move to `planned`; retain Python names, tests, docs and original notes, with
  the required RP-22F responsible-task prefix. Missing synchronous laboratory
  paths: `tk-rackpy-22f-laboratory-python-paths-for-audio-and-65ccf3` (blocks RP-22).
- Approved correction: 21 borrowed-host-view-only UI virtuals move to `planned`,
  preserving evidence and prefixing RP-28F responsibility. Missing UI-safe
  subclass trampolines: `tk-rackpy-28f-python-subclass-trampolines-for-plugw-154b61`.
- The five audio::Port stream virtuals stay implemented: synchronous overrides
  exist only on Python-driven devices. Four Module driver-strategy virtuals
  and all six engine/ten mixed virtuals keep their real laboratory paths.
- Three CableWidget stale notes now cite the RP-28E trampoline; Quantity.getString
  notes now distinguish its real QuantityTrampoline override from a borrowed
  ParamQuantityView helper. No status changed for these four records.
- Same-object reentry divergence remains open under
  `tk-rackpy-trampolines-suppress-same-object-re-entra-0abce9`. Neither the
  shared helper nor RP-22E's pinned test was modified.

Linux report before: implemented 2942, planned 2, unreviewed 1, excluded_internal 111,
absent 2; after: implemented 2888, planned 56, unreviewed 1,
excluded_internal 111, absent 2. Zero verified before and after.
The 54 status corrections are not removals from the 3058-record denominator.

### Exact corrected records (implemented → planned)

#### driver (33)

- `method:rack::audio::Device::getBlockSize()`
- `method:rack::audio::Device::getBlockSizes()`
- `method:rack::audio::Device::getName()`
- `method:rack::audio::Device::getNumInputs()`
- `method:rack::audio::Device::getNumOutputs()`
- `method:rack::audio::Device::getSampleRate()`
- `method:rack::audio::Device::getSampleRates()`
- `method:rack::audio::Device::setBlockSize(int)`
- `method:rack::audio::Device::setSampleRate(float)`
- `method:rack::audio::Device::subscribe(rack::audio::Port *)`
- `method:rack::audio::Device::unsubscribe(rack::audio::Port *)`
- `method:rack::audio::Driver::getDefaultDeviceId()`
- `method:rack::audio::Driver::getDeviceIds()`
- `method:rack::audio::Driver::getDeviceName(int)`
- `method:rack::audio::Driver::getDeviceNumInputs(int)`
- `method:rack::audio::Driver::getDeviceNumOutputs(int)`
- `method:rack::audio::Driver::getName()`
- `method:rack::audio::Driver::subscribe(int, rack::audio::Port *)`
- `method:rack::audio::Driver::unsubscribe(int, rack::audio::Port *)`
- `method:rack::midi::Device::getName()`
- `method:rack::midi::Driver::getDefaultInputDeviceId()`
- `method:rack::midi::Driver::getDefaultOutputDeviceId()`
- `method:rack::midi::Driver::getInputDeviceIds()`
- `method:rack::midi::Driver::getInputDeviceName(int)`
- `method:rack::midi::Driver::getName()`
- `method:rack::midi::Driver::getOutputDeviceIds()`
- `method:rack::midi::Driver::getOutputDeviceName(int)`
- `method:rack::midi::Driver::subscribeInput(int, rack::midi::Input *)`
- `method:rack::midi::Driver::subscribeOutput(int, rack::midi::Output *)`
- `method:rack::midi::Driver::unsubscribeInput(int, rack::midi::Input *)`
- `method:rack::midi::Driver::unsubscribeOutput(int, rack::midi::Output *)`
- `method:rack::midi::Input::onMessage(const rack::midi::Message &)`
- `method:rack::midi::InputQueue::onMessage(const rack::midi::Message &)`

#### ui (21)

- `method:rack::app::PlugWidget::step()`
- `method:rack::app::RackScrollWidget::draw(const rack::widget::Widget::DrawArgs &)`
- `method:rack::app::RackScrollWidget::onButton(const rack::widget::Widget::ButtonEvent &)`
- `method:rack::app::RackScrollWidget::onHover(const rack::widget::Widget::HoverEvent &)`
- `method:rack::app::RackScrollWidget::onHoverKey(const rack::widget::Widget::HoverKeyEvent &)`
- `method:rack::app::RackScrollWidget::onHoverScroll(const rack::widget::Widget::HoverScrollEvent &)`
- `method:rack::app::RackScrollWidget::step()`
- `method:rack::app::RackWidget::draw(const rack::widget::Widget::DrawArgs &)`
- `method:rack::app::RackWidget::onButton(const rack::widget::Widget::ButtonEvent &)`
- `method:rack::app::RackWidget::onDragEnd(const rack::widget::Widget::DragEndEvent &)`
- `method:rack::app::RackWidget::onDragHover(const rack::widget::Widget::DragHoverEvent &)`
- `method:rack::app::RackWidget::onDragStart(const rack::widget::Widget::DragStartEvent &)`
- `method:rack::app::RackWidget::onHover(const rack::widget::Widget::HoverEvent &)`
- `method:rack::app::RackWidget::onHoverKey(const rack::widget::Widget::HoverKeyEvent &)`
- `method:rack::app::RackWidget::step()`
- `method:rack::app::Scene::draw(const rack::widget::Widget::DrawArgs &)`
- `method:rack::app::Scene::onDragHover(const rack::widget::Widget::DragHoverEvent &)`
- `method:rack::app::Scene::onHover(const rack::widget::Widget::HoverEvent &)`
- `method:rack::app::Scene::onHoverKey(const rack::widget::Widget::HoverKeyEvent &)`
- `method:rack::app::Scene::onPathDrop(const rack::widget::Widget::PathDropEvent &)`
- `method:rack::app::Scene::step()`

## Failure/lifetime coverage

Each new exception, superclass recursion, between-object recursion and stale
close/delete case has its own actual Rack subprocess in
`tests/laboratory/test_races.py`. A crash/signal/timeout fails the outer test.
The shutdown scenarios keep laboratory native holders and an override/bench
cycle alive until interpreter finalization, race a worker's rejected bench
requests against main-thread use, and require a joined worker in headless
and live-context Rack. Supervisor-approved shutdown variants assert both the
documented safe refusal to finalize while native override holders genuinely
remain alive (normal Rack exit, leak diagnostic, no Py_FinalizeEx), and clean
`Py_FinalizeEx returned 0` after releasing those holders in a closing hook.
The first case is designed refusal, not a defect.

Normal-mode registration refusal is already tested by
`tests/laboratory/in_rack/test_bench.py::test_normal_mode_refuses_python_audio_callbacks`:
class definition refuses process/process_bypass/step and engine/driver events;
`ModuleClass.factory()` refuses a laboratory class, so patches cannot create
one. The existing startup-disabled/mixin/monkeypatch/Rack-thread refusal tests
remain in the closing suite.

## Commands and results

Every command below ran in this worktree. Host output is captured with
`set -o pipefail; ... 2>&1 | tee build/validation/<name>.log`, recording
`${PIPESTATUS[0]}` separately. Before any tests started, an initial queued
full host invocation was cancelled before acquiring the lock (empty log,
exit 143), to finish the manifest corrections; no tests from it are claimed.
A queued manual debug invocation was likewise cancelled before taking the
lock (exit 143), in favor of the standard pytest targeted rerun below.

| Command | Result / persistent evidence |
| --- | --- |
| `uv sync --frozen` | Pass: Python 3.14.2 environment, 53 pinned packages installed. First command in this worktree. |
| `CMAKE_BUILD_PARALLEL_LEVEL=3 bin/rackpy-dev build --werror --test-hooks` | Pass, CTest 6/6, repeated after manifest regeneration; `build-final.log`, exit 0. |
| `uv run pytest -m unit` | Pass: 95 passed, 197 deselected, 31.67 s; `unit-final.log`. |
| `prek run --all-files` | Pass, all 14 hooks; `prek-final.log`. Repeated when staging the new artifacts. |
| `bin/rackpy-dev typecheck` | Pass: strict mypy 259 source files plus nine individual CLI checks; `typecheck-final.log`. |
| `bin/planning-check` | Pass: 45 tasks, 114 dependencies, acyclic; TASKS.md and witan.json agree. |
| `uv run bin/generate_bindings.py check` | Pass: generated mapping, registration metadata, coverage and callback paths match the manifest. |
| `uv run bin/check_coverage.py check --without-built-package` | Pass: absent 2, excluded_internal 111, implemented 2888, planned 56, unreviewed 1. Built-package introspection is separate (three tests passed in the full host run). |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` | Pass: serial, zero warnings; `docs-build-final.log`, exit 0. |
| `uv run pytest -m 'docs and not host'` | Pass: 15 passed, 277 deselected, 128.88 s (2m08s); `docs-tests-final.log`, exit 0, after fixing the throwaway-site planning link fixture. |
| `RACKPY_LABORATORY_COST_OUT=benchmarks/rp22-laboratory-cost.json CMAKE_BUILD_PARALLEL_LEVEL=3 flock /tmp/rackpy-host-tests.lock timeout 5400 uv run pytest -m 'host and not dist' -ra --basetemp=build/validation/host -o tmp_path_retention_policy=all` | **Failed**: 118 passed, 4 failed, 170 deselected, 1483.07 s (24m43s), exit 1; `host.log` / `host.exit`. The four failures are the new shutdown setup error described below. All other suites, benchmark and isolated race probes passed. |
| `CMAKE_BUILD_PARALLEL_LEVEL=3 flock /tmp/rackpy-host-tests.lock timeout 2400 uv run pytest tests/laboratory/test_races.py -k shutdown -ra --basetemp=build/validation/shutdown-final -o tmp_path_retention_policy=all` | Pass: 4 passed, 4 deselected, 878.83 s (14m38s), exit 0, fresh production and test-hooks builds; `shutdown-final.log` / `shutdown-final.exit`. |

### Full host failures and correction

The first full run exposed a test setup mistake: the shutdown entry module
constructed `midi.Output()` at plugin startup, before Rack binds a live UI
context, even in windowed mode. Rack correctly raised `ContextError`:
`midi.Output() needs Rack's live bound UI context (MIDI ports timestamp messages with its engine)`.
The two headless variants exited without reaching the required armed/closing
markers; the two windowed variants never installed their close-window callback
and reached the 120-second Rack watchdog. All four tests failed visibly; none
was skipped or marked xfail. Retained logs are under
`build/validation/host/test_shutdown_with_laboratory_{0,1,2,3}/log.txt`.

Correction: keep the laboratory device alive at startup; only the live-context
scenario creates and connects a MIDI output from the UI step. The headless
scenario has no port (that API legitimately needs a context). Both variants
still race the worker's refused bench attempts and retain laboratory objects.

The corrected targeted run passed all four variants. The two deliberately
retaining override holders each log the designed refusal with **3 native-held
Python references and 0 transferred objects**, no Py_FinalizeEx, and normal
Rack exit. The two closing-hook cleanup variants each log
`Py_FinalizeEx returned 0`. The live-context variants also log the connected
output marker. All four report `LAB SHUTDOWN closing hook passed`, with no
closing-hook exception; each hook joined the competing worker and asserted
that only the two main-thread calls reached the Python process override.
Logs remain under `build/validation/shutdown-final/test_shutdown_with_laboratory_{0,1,2,3}/`.
No test or implementation source changed after this passing targeted run.

**Supervisor-approved validation amendment:** RP-22 cannot close while RP-22F
owns the missing callback paths. RP-22's closing full-suite rerun is deferred
to RP-22F (the last callback child), rather than reporting this failed full run
as green or running a second full suite for RP-22D. The corrected four-case
targeted run is required on the final test code. No full all-green host result
is claimed by RP-22D.

The intentional deadlock probe
`tests/integration/test_lifecycle_probe.py::test_unsafe_worker_deadlocks_and_is_caught_by_the_watchdog`
**passed** in the full run: its own 30-second watchdog deliberately kills the
unsafe worker's Rack process, expects `returncode is None`, and checks that
`onReset` acquired the engine lock without obtaining the GIL. This is distinct
from the two unintended 120-second setup timeouts above; the outer 5400-second
suite timeout was not reached.

### Embedded raw counts from the first full run

| Laboratory suite | Passed | Skipped | Meaning |
| --- | ---: | ---: | --- |
| Production at plugin startup | 34 | 5 | Five tests require the live context/test hooks; outer wrapper requires exactly those skips. |
| Live-context, Xvfb/test hooks | 41 | 1 | The no-context parameter-event test is covered by the startup run instead. |
| Callback benchmark | 1 | 0 | All 84 timed runs retained in the JSON. |
| Four isolated exception/super/cross-object/stale subprocesses | 4 | 0 | One passed in-Rack test in each of four Rack processes. |

Each corresponding `in_rack_result.json` under `build/validation/host/` reports
exit 0. The original normal-mode process-registration rejection tests ran in
both laboratory suites.

### Other intermediate failures (fixed, not hidden)

- The first new inventory unit test incorrectly counted CableWidget's UI
  virtuals as three instead of five (three stale RP-28D notes, plus onAdd and
  onRemove). That run was 94 passed, 1 failed; the exact assertion now pins five,
  and the final unit run passed 95/95.
- Initial strict typing found an invalid template object as a static class
  base and an `object`-typed timing conversion. The shutdown probe now retains
  the concrete generator instance; benchmark raw runs use `CostRun` TypedDict.
- The first docs build had two warnings because the raw benchmark file did
  not exist yet; after actual measurement it built without warnings.
- The first docs-test run was 14 passed, 1 failed (277 deselected, 126.29 s):
  the throwaway docs fixture did not link `planning/`, so the new validation
  link could not resolve. `tests/docs/conftest.py` now includes that real
  repository input, rather than weakening warning checks or removing the link.

## Measured callback costs (diagnostic only)

Raw data: `benchmarks/rp22-laboratory-cost.json`, produced by the passing
benchmark within the full suite. Intel Core i9-14900K, Linux 7.3.0-4-cachyos,
GCC 16.2.1, bundled CPython 3.14.9, Rack Free 2.6.6, powersave /
balance_performance, turbo enabled, CPUs 0–31, load average 3.34 at collection.
The file retains the exact compiler command, revision and benchmark/native
source SHA-256 (verified against the final files), randomized execution order,
all elapsed times and per-frame averages. Native laboratory implementation
is unchanged from base 7d2c543; the benchmark is added in this worktree.

One module/bench, 48 kHz, zero parameters/ports/channels/cables, 2000 warmup
frames, 21 repetitions of 20000 frames per variant, shuffle seed 22. Baseline:
an ordinary empty native RackPy Module on the same Bench, no kernel. Medians
of run-average nanoseconds per frame:

| Variant | ns/frame |
| --- | ---: |
| Native empty RackPy Module | 3.05505 |
| Laboratory module, Rack base process/step | 4.306 |
| Empty Python process override | 135.8127 |
| Python process calling super().process | 266.81555 |

These are offline Bench/trampoline diagnostics, **not production real-time
performance**. The GIL is already held on the main thread; no other Python
thread competes, no device or audio clock runs, no worker scheduling/deadline
is measured. Timings include Bench scheduling and argument conversion.
Run-average distributions are not individual-callback tail latencies. No
performance budget or real-time safety claim follows from these numbers.

## Remaining gates

Linux x86_64 only. macOS/Windows, Rack Pro, actual audio/MIDI hardware,
driver scheduling, controlled-hardware real-time budgets, sanitizers,
distribution/toolchain packaging and release publication are not exercised.
RP-22F and RP-28F paths plus shared-helper reentry remain full-release blockers.

## After integration onto RP-23C

Rebased by the supervisor onto `f2020d3` (RP-23C): RP-22D, its review fix
(the benchmark records its actual invocation) and the trampoline re-entrancy
fix, in that order, with generated files regenerated from the merged
manifest (implemented 2890, planned 54, unreviewed 1, excluded_internal 111,
absent 2) and the re-entrancy limitation text in
`docs/explanation/callbacks.md` and the callback-paths generator replaced by
the fixed behaviour. Independent read-only reviews: RP-22D approved with one
provenance nit (fixed), the re-entrancy fix approved with no findings. On the
combined state: `bin/rackpy-dev build --werror --test-hooks` (CTest 6/6),
`uv run pytest -m unit` (100 passed), `prek run --all-files`,
`bin/rackpy-dev typecheck`, `bin/planning-check`,
`uv run bin/generate_bindings.py check`, the sphinx `-W` build,
`uv run pytest -m "docs and not host"` (15 passed),
`uv run pytest -m "native and not host"` (32 passed) and the full
`flock /tmp/rackpy-host-tests.lock uv run pytest -m "host and not dist"`
(123 passed, 178 deselected, 821 s) all pass.
