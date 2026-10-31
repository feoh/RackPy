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

## Plugin state-handler typing

`ModuleClass.from_json` and plugin settings input handlers now receive the
recursive JSON value type instead of `Any`; output handlers return `object`,
which the native converter validates. This avoids mutable-container invariance
rejecting valid `list[str]` state while still requiring consumers to narrow
untrusted input. Strict mypy and scoped pyright pass; actual Rack plugin API
and create/remove/save/reload host tests passed with current RP-32 Python
overlaid on the hooks build. The underlying state conversion was not changed.

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

## Walkthrough, keyword-only options and the facade snapshot

A walkthrough of the four how-to guides and their 30 runnable snippets found
that the guides taught `Any` for module state, settings, event and drawing
handlers, although typed equivalents exist. The snippets now use
`rackpy.types.JSON`, `Module.ResetEvent`, `w.DrawArgs` and `w.ButtonEvent`.
Adding `docs/snippets` to strict mypy exposed one real gap in a snippet
(`ParamQuantity.get_param()` may return `None`) and one unsafe teaching
pattern: narrowing `data.get("text")` does not narrow a separate
`data["text"]` lookup, so `register_models.py` now validates the value it
assigns. Two other reports were mypy narrowing artifacts across
`remove_child()`/`close()`; only `warn_unreachable` is disabled for
`docs.snippets.*`, with the reason in `pyproject.toml`. Strict pyright also
passes on the snippets and they are in its gated scope.

A scan of every public facade signature found options accepted positionally.
`channel`/`frame` (MIDI message helpers), `inputs`/`outputs`/`devices`
(`virtual_driver`), `cookies` (network helpers), `depth` (`entries`) and
`level` (`archive`) are now keyword-only; operands stay positional. One test
call, `control_change(7, 127, 15)`, relied on a positional channel and was
updated. `audio.Stream` gained a repr that cannot raise on a deleted device.
Real-host tests check the parameter kinds with `inspect.signature`, a
positional channel's `TypeError` and the repr.

`bin/facade-surface` (cyclopts, `ast` only) records the names, signatures,
class bases and re-export origins of all 272 names in 33 public facade
modules in `tests/facade/surface.json`; `check` runs as a prek hook and a unit
test, and a second test proves that changed, added and removed entries are
reported. `docs/reference/api-stability.md` defines the public facade, the
reviewed conventions and the typing scope. Its release compatibility policy is
explicitly labelled as a proposal needing owner approval (RP-44); nothing is
released or promised.

Validation: prek (all hooks, including mypy over 320 files and both new
hooks), Sphinx `-W -n`, and in actual Linux Rack with current RP-32 Python
overlaid on the identified RP-31 integration binaries: `tests/docs/test_snippets.py`
(every snippet, in Rack and in Python), `tests/io/test_virtual.py` and
`tests/services/test_system.py`, 7 outer tests passed.

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

## Drawing facade return contract

`Painter.text` with a width returns `x + width`, not the rendered first-line
advance. Its docstring now matches its implementation; the existing
framebuffer test uses a nonzero x to distinguish the two. The rendering suite
passed in actual Linux Rack under Xvfb/Mesa with current RP-32 Python sources
overlaid on the hooks build; this is not GPU or cross-platform evidence.
`Painter`'s input is now typed as a graphics context or an object with a `vg`
property (`DrawTarget`), rather than `Any`. Mypy and pyright check real
draw-argument and context types plus an intentionally invalid object, which
remains rejected at runtime. A first docs build failed because the protocol
was private and could not be linked; making it a documented public protocol
fixed the Sphinx `-W -n` build, and the host rendering suite was rerun.

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
