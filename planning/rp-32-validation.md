# RP-32 facade audit checkpoints

## Initial checkpoint (2026-10-30)

RP-32 is **in progress**. Corrected the package's stale assertion that no Rack
API is bound, without changing the private/do-not-upload package status. Added
an explicitly provisional raw-to-facade discovery page. Its table is a guide,
not a per-symbol coverage or completion claim.

`audio.open_port` and `midi.open_input` previously returned an unconstrained
generic type when `kind` was omitted, with a suppressed default-type error.
Overloads now expose `Port` and `InputQueue` respectively for default calls,
and retain the precise subclass for an explicit `kind`. Runtime behavior is
unchanged. `tests/facade/typing_contract.py` checks both paths with mypy's
`assert_type`, without loading the Rack host.

Validation: strict mypy, pinned pyright 1.1.408 in strict mode on the typing
contract, all prek hooks, and Sphinx `-W -n` passed. The pyright sample config
suppresses only missing Python source for the intentionally stub-only native
modules; unknown-type diagnostics remain enabled. This is not a pyright pass
on every facade or example. Existing
`tests/io/test_virtual.py` and `tests/io/test_io_paths.py` passed under the
Linux Rack host lock (two outer tests). These host runs used the already-built
RP-31 integration production and hooks binaries; there was no native change in
this checkpoint. They do not establish macOS or Windows behavior.

## JSON facade typing follow-up

The network facade now types results as the existing recursive
`rackpy.types.JSON` rather than `Any`. Request bodies use `object` and are
validated by the native JSON converter: an initial `JSON` input annotation
incorrectly rejected ordinary typed `list[str]` values because mutable lists
are invariant. This trade-off is documented rather than hiding it with `Any`. The binding's `fromJsonAt`
implementation constructs only JSON values. Tests narrow returned values before
indexing them, and the reference notes that `None` cannot distinguish failure
from JSON null. Mypy and scoped strict pyright pass the additional type
assertions; `tests/services/test_system.py` passed against the Linux Rack host
and existing integration binaries. The raw binding's own `Any` signature is
not yet changed, and this does not complete the broad typing audit.

## Lifecycle callback typing follow-up

`ModuleClass` now annotates its eight UI lifecycle callbacks with the matching
nested Rack `Module` event types instead of `Any`. Scoped mypy and pyright
contracts exercise subclass overrides and reject an incorrect port-change
event. `tests/plugin/test_plugin.py` passed nine outer tests in the Linux Rack
host, using the integration binaries. The pyright contract is now a prek hook;
it is still scoped to the contract file, not the entire project.

## MIDI convenience-cost probe

`tests/facade/test_benchmark.py` runs an alternating-order UI-thread probe in
the Linux Rack host: `rackpy.midi.message` with three bytes and frame `-1`
versus direct `Message()` construction followed by the same byte and frame
assignments. It first checks that the results agree. Seven 20,000-message
runs, machine details, clock and exact baselines are in
`benchmarks/rp32-midi-facade-cost.json`. This is one shared-host diagnostic,
not a performance guarantee, and it does not measure the audio path or all
facade operations. The production binary and source revision are identified in
the artifact; no native changes were part of this checkpoint.

## Offline graph typing

The static contract also checks graph inputs, arithmetic signals and plans
with mypy and strict pyright. The existing pure-Python graph and graph-codegen
suites passed 25 tests without a Rack host; this is not an actual-host audio
walkthrough.

Still to do: a complete coherent facade and typing review, raw discoverability,
strict mypy and pyright samples, signature and exception tests, representative
user walkthroughs, measured wrapper overhead with explicit baseline, any
resulting fixes, and versioning/freeze decision. Do not close RP-32 on this
checkpoint.
