# RP-31J checkpoint — incomplete

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
Sphinx `-W -n` also passed. This does not settle startup/rate-change
equivalence or the counted silence when a converter is not ready; the task
remains incomplete.

The driver/engine bridge and MIDI queue now require their atomics to be
always lock-free at compile time (indices, counters, state and handover
pointers), so a target that implements them with library locks cannot silently
build these paths. This is a compile-time guard, not a proof that every Rack or
driver call is nonblocking. Linux x86_64 builds and CTests passed; the other
architectures still require their own builds.

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
