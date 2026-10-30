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

The network facade now types request bodies and results as the existing
recursive `rackpy.types.JSON` rather than `Any`. The binding's `fromJsonAt`
implementation constructs only JSON values. Tests narrow returned values before
indexing them, and the reference notes that `None` cannot distinguish failure
from JSON null. Mypy and scoped strict pyright pass the additional type
assertions; `tests/services/test_system.py` passed against the Linux Rack host
and existing integration binaries. The raw binding's own `Any` signature is
not yet changed, and this does not complete the broad typing audit.

Still to do: a complete coherent facade and typing review, raw discoverability,
strict mypy and pyright samples, signature and exception tests, representative
user walkthroughs, measured wrapper overhead with explicit baseline, any
resulting fixes, and versioning/freeze decision. Do not close RP-32 on this
checkpoint.
