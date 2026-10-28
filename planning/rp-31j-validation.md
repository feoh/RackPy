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

The host suite now reaches the differential: 8 inner tests pass, but
`test_the_device_hears_the_same_as_through_core_audio[1-core]` fails. The first
block has a one-frame startup difference between Core and the bridge; later
blocks shown in the failure match. Do not waive or weaken this comparison:
investigate whether the discrepancy is initialization or real audio timing,
and prove any fix against Core. Timed MIDI live-Rack differential, interpreter
and compiled graph paths, audio-path guard, sanitizer/stress, documentation,
manifest and full task validation remain. The checkpoint is not evidence that
the production I/O paths are correct or real-time safe.
