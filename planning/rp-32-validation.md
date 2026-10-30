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
host, using the integration binaries. The pyright contract is now a prek hook.
A follow-up expanded its strict scope from the contract file to all files in
`tests/facade/` and 33 explicitly listed facade modules that pass strict
pyright. Inclusion means only that the static check passed, not that each
module has received a complete behavioral review; the check does not cover the
entire project.

## MIDI convenience-cost probe

`tests/facade/test_benchmark.py` runs an alternating-order UI-thread probe in
the Linux Rack host: `rackpy.midi.message` with three bytes and frame `-1`
versus direct `Message()` construction followed by the same byte and frame
assignments. It first checks that the results agree. Seven 20,000-message
runs, machine details, clock and exact baselines are in
`benchmarks/rp32-midi-facade-cost.json`. This is one shared-host diagnostic,
not a performance guarantee, and it does not measure the audio path or all
facade operations. The production binary and source revision are identified in
the artifact; no native changes were part of this checkpoint. A subsequent
`uv run pytest -q -m unit` on the RP-32 tree passed 118 tests (215 deselected).

## Duplicate driver and device names

Audio and MIDI name-based driver and device lookups now raise `LookupError`
when the requested name is ambiguous; the name-map `drivers()` helpers also
refuse duplicate keys. ID lookup remains available, and a unique name still
works even if an unrelated name is duplicated. Unit tests exercise the shared
selection helper; actual Rack host tests register duplicate virtual audio and
MIDI drivers and devices, check refusal and select devices by ID. The same
host suite uses `inspect.signature` to check the keyword-only `kind` defaults
and resolves the port-change callback event annotation against the real host
binding.

A fixture correction was necessary during this checkpoint: the reused RP-31
native build's bundle also contained RP-31's Python sources, not this branch's
facade. Earlier host runs therefore did not prove the modified RP-32 Python
code. The corrected fixture overlaid this worktree's `src/rackpy` into the
bundle while keeping the identified native binaries; it was restored after
use. With that correction, 12 outer virtual-I/O, services, plugin and engine
host tests and two production I/O/benchmark outer tests passed. The earlier
benchmark used the RP-31 version of `midi.message`, which is unchanged here;
its artifact identifies that baseline. Linux only.

## Engine rollback contract

The facade and reference no longer promise that every failed operation leaves
the engine unchanged. `undo_step` can only undo the actions actually recorded;
undo or history-push failure and unrecorded side effects are not transactionally
covered. A real windowed Linux Rack host test now checks that a recorded change
is undone while an unrecorded change survives the same exception. An initial
probe passed an ID where `set_param_value` expects a module and failed with
`TypeError`; correcting it to pass the module made `tests/engine/test_host.py`
pass. This changes documentation and tests, not the rollback implementation.

## Offline graph typing

The static contract also checks graph inputs, arithmetic signals and plans
with mypy and strict pyright. `graph.py` is included in the strict pyright
scope: its two file-local suppressions cover module-internal collaboration on
private attributes and runtime type checks for untyped callers; unknown-type
diagnostics remain enabled. Five additional facade files use targeted pyright
suppressions for runtime validation of untyped callers, intended private native
adapter calls, a mypy-required cast, and a native-invoked shutdown hook;
`filters.process_block` still has its explicit `Any` gap. The existing pure-Python graph and graph-codegen
suites passed 25 tests without a Rack host; this is not an actual-host audio
walkthrough.

A diagnostic strict pyright run over `src/rackpy` on 2026-10-30 found 241
errors in 75 files; unknown members, private internal uses and dynamic
`__all__` constructions dominate. This is not a passing full-source check;
the facade-test check above does not suppress these diagnostics globally.

Still to do: a complete coherent facade and typing review, raw discoverability,
strict mypy and pyright samples, signature and exception tests, representative
user walkthroughs, measured wrapper overhead with explicit baseline, any
resulting fixes, and versioning/freeze decision. DSP bulk filter typing and
dynamic IIR template stubs still use `Any`; a structural protocol that accepts
unsupported native filter objects would be misleading. Do not close RP-32 on
this checkpoint.
