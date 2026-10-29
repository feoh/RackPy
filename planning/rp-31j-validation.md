# RP-31J checkpoint — incomplete

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
an already-fed secondary port; that explanation has not yet been isolated
against two Core ports. The comparison is therefore evidence for the
preconfigured-rate case only, not proof of startup/rate-change equivalence.
The existing separate rate-change test checks resampling and missed converters.

Timed MIDI live-Rack differential, interpreter and compiled graph paths,
audio-path guard, sanitizer/stress, documentation, manifest and full task
validation remain. This checkpoint is not evidence that the production I/O
paths are correct or real-time safe.
