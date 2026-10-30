# RP-31J validation — bounded implementation

The approved native design drops and counts MIDI messages longer than three
bytes and can emit counted silence when a resampler is unavailable. It is not
full SysEx or Core rate-transition equivalence. No manifest record is promoted
to verified by this task, and these limitations do not waive the full-surface
release gate. Linux x86_64 in Rack Free 2.6.6 is the host evidence here; physical
audio devices, macOS and Windows were not tested.

## Anticipating fixed sample-rate changes

UI maintenance prepares converters for Rack's requested fixed engine rate in
separate future slots, without displacing converters for the applied rate. A
live Rack test requests 96 kHz while the engine is still at 48 kHz, runs
maintenance, then lets Core apply the rate in its driver callback: the bridge
reports no resampler misses and matches Core after the characterized transition.
Another live test starts with a mismatched 44.1 kHz engine, requests 96 kHz,
and exercises both rates without another UI step; neither converter misses.
Initially a single-slot implementation failed that case with two misses when
prewarming displaced the current converter. Native handover tests cover both
slots; 36 selected Rack-host, introspection, native/sanitizer and docs tests
passed after both Werror builds. An independent read-only review confirmed
ownership but identified a race when a future-slot tag and pointer were
replaced separately. The handover now uses a nonblocking atomic reservation so
the driver cannot take a replacement pointer under the old tag. A separate
read-only review found no safety issue in the reservation or memory ordering;
it did not execute tests. If maintenance is concurrently holding the
reservation, the driver still does not wait and can
count/silence a miss; not every anticipated rate transition is guaranteed
gap-free. On 2026-10-30 the owner chose to keep this counted-silence fallback
and continue the bounded RP-31J design after reviewing the transition trade-off
and experimental timings below. This decision does not establish full Core
transition equivalence or waive the project's full-coverage release gate.
After the reservation change, Linux production and hooks Werror
builds and 7 CTests each passed; seven selected native and live-Rack tests,
including the ASan/UBSan and TSan concurrent handover stress, also passed.
The stress test was then strengthened to alternate two different future
settings concurrently with the driver's three settings; the production Werror
build and all six native I/O tests, including both sanitizer variants, passed.
The fixed-rate live comparison now covers both RackPy and Core as master.
Prek and the repository's strict mypy command passed, as did 146 unit/native
tests, generated-file and source-only coverage checks, and Sphinx. An
exploratory unconfigured `basedpyright` invocation failed on repository-wide
stub/import and test-type diagnostics; it is not the project's mypy gate. The
live Rack I/O suite passed five additional independent runs after the two-slot
change. This cannot guarantee anticipation if
the driver observes a change before the UI step, or for arbitrary device/rate
changes; the documented counted-silence fallback remains. Linux hooks and
production Werror builds and 7 CTests passed, as did the live Rack I/O suite.

## Experimental Speex state reuse (not implemented)

The pinned Rack submodule records SpeexDSP commit
`27ddf98b627ab425d5337b1a0d8b80d7528f9b30`. Its `update_filter` retains
sinc-table and history capacity but can call `realloc` if either is too small.
A throwaway Linux x86_64 probe linked to the SDK's `libRack.so` initialized
one oversized single-channel state per channel at Core Audio's quality 6,
then used public `reset_mem` / `set_rate` / `reset_mem` and compared samples and consumed/output
counts against freshly initialized multi-channel states. Three repetitions of
seven selected rate pairs and channel counts 1, 2, and 16 matched bit-for-bit;
the audio guard saw zero intercepted heap, mutex, write and GIL calls during
those particular transitions. This is **not** proof for other rates, histories,
channel changes, platforms, or real-time deadlines. A first probe reusing one
oversized 16-channel state failed the comparison: Speex's `reset_mem` clears a
contiguous region that need not cover every channel when capacity is larger
than the active filter's history. Do not use that approach.

Exploratory single-run transition timings on an Intel i9-14900K (microseconds;
`fresh16` destroys and initializes one 16-channel Speex state;
`reuse16x1` resets and retunes 16 overprovisioned one-channel states):

| input → output Hz | fresh16 | reuse16x1 |
| --- | ---: | ---: |
| 48000 → 44100 | 20 | 324 |
| 48000 → 96000 | 2 | 37 |
| 44100 → 48000 | 18 | 294 |
| 96000 → 44100 | 20 | 325 |
| 768000 → 11025 | 150 | 1313 |
| 11025 → 768000 | 26 | 310 |
| 48000 → 44100 | 20 | 325 |

These are raw single observations from an isolated probe, not a benchmark or a
claim about production latency. Table generation on the driver thread may
consume a substantial part of an audio deadline. Bounds, memory cost, actual
Rack parity, all-channel transition state, allocator instrumentation, and
platform behavior still require proof before considering this alternative.

## Audio master decision and MIDI queue

The native MIDI queue uses a fixed-capacity heap to order pending messages
without shifting the whole waiting list on each out-of-order insertion. A
native differential test fills the queue, compares frame and same-frame arrival
ordering with Rack's `InputQueue`, and adds messages while future ones remain
queued. A production-path audio guard also exercises a full 8,192-message
reverse-frame burst through the queue: ordering is correct and the guard reports
no intercepted forbidden calls. Linux hooks and production warnings-as-errors
builds and 7 CTests, all 39 native tests including I/O guard/sanitizers, and
selected production Rack-host and introspection tests passed. No timing or
speedup has been measured.

Rack 2.6.6's `Engine::stepBlock` takes `blockMutex` and a shared engine lock;
`setMasterModule` and rate suggestion can also lock or dispatch events. Its
`audio::Device::processBuffer` takes `processMutex` around port callbacks.
An earlier mistaken interpretation treated Rack-owned locks as forbidden and
led to a temporary secondary-only bridge. The owner clarified that the rule
forbids **additional RackPy blocking locks**, not Rack's existing synchronization,
and then explicitly chose to restore RackPy's device-clocked master behavior.
The bridge again invokes Rack's public engine clock as Core Audio does; it must
not claim the complete host path is lock-free. Fresh Linux production and hooks
Werror builds each passed 7 CTests, and 36 selected Rack-host, introspection,
native/sanitizer, and documentation tests passed. The in-Rack I/O suite covers
both Core and RackPy as master with one and four engine threads, and checks a
standalone RackPy port claims master and clocks its engine block. A subsequent
live run also verifies stream stop releases master and restart reclaims and
clocks it; manifest evidence includes the two stream callbacks. A fresh
production-build run of I/O, package introspection and docs tests passed all
21 selected tests after that evidence update.

## Production build and empty-direction safety

The Linux warnings-as-errors production build completed and passed all 7
CTests; production built-package introspection and documentation tests passed
in Rack. A 35-test run also covered the I/O host and native suites, MIDI and
kernel-library loader, using the production build for `dev_build` and the
separate hooks build for `hooks_build`; temporary fixture overrides were
restored. The same production and hooks builds passed 7 CTests after fixing
zero-stride `FrameRing` spans to return `nullptr` without pointer arithmetic on
an unallocated buffer. A native test exercises an output-only bridge and the
empty ring. The 35-test run was repeated after that fix; 107 unit tests,
prek, strict typecheck, generated-file and source-only coverage checks, and
Sphinx `-W -n` also passed. This did not settle startup/rate-change
equivalence; the owner subsequently accepted the documented counted-silence
fallback for this bounded task, not as full transition equivalence.

The driver/engine bridge and MIDI queue now require their atomics to be
always lock-free at compile time (indices, counters, state and handover
pointers), so a target that implements them with library locks cannot silently
build these paths. This is a compile-time guard, not a proof that every Rack or
driver call is nonblocking. Linux x86_64 builds and CTests passed; the other
architectures still require their own builds. The live MIDI path now also
checks that a four-byte SysEx message increments `dropped_long` without
preventing the following timed notes from reaching their kernel. This verifies
the current three-byte limit; it does not make long-message semantics complete.

The four affected binding records and generated mapping/callback artifacts now
cite the WIP paths and their evidence without a status promotion. After this
update, the Linux hooks build and 7 CTests, live I/O/graph suites, live package
introspection and docs snippets, Sphinx (`-W -n`), `prek` (including mypy),
generator check, source-only coverage check, 32 unit/codegen and 51 manifest
checks passed. Introspection and snippets used the existing **test-hooks** build
via a temporary fixture override; they are not production-build evidence. A
fresh Werror production build in `/tmp/pytest-of-feoh/pytest-168/build0` timed
out after 37 of 86 remaining steps and is incomplete. Attempts to run the
combined unit/docs selection also timed out while building; orphaned compiler
processes were terminated. The fixture override was restored.

Graph I/O now has interpreter and compiled nodes for device audio and timed
MIDI-CV, with an offline refusal and native/codegen argument checks. In live
Rack, both audio graph tiers matched Core Audio sample-for-sample in the
preconfigured-rate, mono-loopback case using 0.1 multiplication (the graph's
strict division by 10 initially differed by one float32 ULP from Core's build).
Both MIDI graph tiers matched each other at scheduled note frames; the built-in
MidiToCV also matched Core MIDI-CV's 12 outputs each frame for note-on, mod
wheel and note-off delivered via Rack's Loopback driver. A test-only foreign
output reader inspects the Core reference. The audio startup/rate-transition
comparison and wider MIDI cases remain unproven. A later live transition
experiment exercised both master choices with a previously running loopback:
from the third block Core and RackPy matched; in the first, either they matched
or one side was cleared after the previous frame. This characterizes those
runs, not full equivalence across subscriber orders or rate changes. One
resampler miss was recorded during the transition; the experiment runs after
the no-miss tests so it does not contaminate their cumulative counter.

The Werror hooks build/7 CTests, targeted live-Rack IO and graph-library suites,
unit codegen tests, native graph and I/O tests (including sanitizer stress),
and `uv run prek run --all-files` passed. The extended LD_PRELOAD guard
observed no intercepted heap, mutex, write or GIL calls across 100,000 frames
of interpreted and compiled graph I/O with delivered MIDI messages. This is
instrumented-path evidence, not a proof about every Rack or driver path. The
temporary hooks-build fixture override was restored.

The kernel-library loader now negotiates the optional I/O services ABI before
registering a library. A separately loaded native probe confirmed it receives
a non-null I/O frame inside Rack; incomplete and wrong-ABI libraries were
refused as a whole. The Linux Werror hooks build and 7 CTests, live Rack
`tests/dsp/test_runtime.py::test_kernel_libraries_load_and_bad_ones_are_skipped`,
and `uv run prek run --all-files` passed. The temporary hooks-build fixture
selection was restored. This verifies service negotiation, not compiled graph
I/O or full production I/O correctness.

The `rp31j` WIP is not validated for production use. On Linux x86_64,
`CMAKE_BUILD_PARALLEL_LEVEL=2 uv run bin/rackpy-dev build --werror --test-hooks`
completed and all 7 CTests passed. The first build invocation timed out after
100 of 102 steps; the second completed. `prek run --all-files` passed.

The actual-Rack audio differential originally failed in fixture setup because
its module was not associated with a registered RackPy model. The host runner
now registers a test-only model, and a test-only native loopback lets the test
connect Core's foreign module without bypassing production ownership checks.
Loopbacks are disconnected before deleting the module. The temporary test
fixture override selecting the existing hooks build was restored.

The host suite reached the differential but initially failed
`test_the_device_hears_the_same_as_through_core_audio[1-core]`. Instrumentation
showed **512 sample mismatches**, not just one frame: the bridge was silent
for the first block while Core produced audio. Setting Rack's suggested engine
rate to the device's 48 kHz before starting the comparison made all nine
inner tests pass, including the entire first block. The likely explanation is
that a master changes the engine rate while processing device inputs, clearing
an already-fed secondary port. A live-Rack experiment with two Core Audio-2
modules at 44.1 kHz starting a 48 kHz device reproduced the divergent first
block in one master selection; the other selection matched. Because Rack
iterates subscribers in pointer order, the test accepts either first-block
outcome, requires the silent/non-silent distinction when they differ, and
requires equality from the third block. It passes with both master choices.
The RackPy-versus-Core comparison remains evidence for the preconfigured-rate
case only, not proof of startup/rate-change equivalence. The separate
rate-change test checks resampling and missed converters.

`tests/io/test_io_native.py` passed all five cases against the existing
hooks build: native audio differential, the instrumented audio guard,
30,000-block ASan/UBSan and TSan stress, and object-level Python symbol checks.
The guard covers its instrumented native paths, not every Rack or driver path;
the temporary fixture override was restored.

A WIP MIDI binding attaches a disconnected, empty Python `InputQueue` to a
module's native queue and exposes queue counters. In actual Rack, a direct
note-on and a Loopback-driver note-off reached `MidiToCV` at their specified
engine frames; attachment, detachment, ownership and dropped-port paths were
also exercised. The original MIDI host suite still passes. A concurrent producer
reservation fixes a race in the 8192-message limit; a four-producer saturation
case passed both sanitizer stress runs. The Werror test-hooks build and 7 CTests,
`tests/io/test_io_paths.py`, `tests/io/test_midi.py`, `tests/io/test_io_native.py`,
prek and Sphinx passed. This is not yet a Core MIDI-CV differential, and the
binding's concurrent lifecycle and real-time path need further review.

Timed MIDI Core differential, interpreter and compiled graph paths, remaining
documentation, manifest and full task validation remain. This checkpoint is not evidence that all production I/O
paths are correct or real-time safe.
