# Adding bindings

How a family task (RP-12 onward) binds part of the Rack API and proves it. The
rules behind this are the [coverage contract](coverage.md); the design is
[ADR 008](decisions/008-binding-manifest.md).

## The pieces

| File | Who writes it | What it holds |
| --- | --- | --- |
| `upstream/api-surface.json` | `bin/inventory_sdk.py` | Every declaration in the pinned SDK: the denominator |
| `bindings/manifest.json` | **you, by hand** | One record per counted declaration: status, Python names, evidence |
| `bindings/exclusions.json` | reviewers | The declarations RackPy deliberately does not bind, each with a policy and evidence |
| `upstream/engine-locks.json` | reviewers | Which engine lock each `Engine` method takes |
| `native/bindings/<family>.cpp` | **you, by hand** | The pybind11 adapters for one family |
| `bindings/generated/`, `native/bindings/generated/`, `docs/reference/coverage.md` | `bin/generate_bindings.py generate` | Derived files; never edit them |

## A record

```json
{"id": "method:rack::engine::Port::getVoltage(uint8_t)", "fingerprint": "…", "status": "implemented",
 "python": {"raw": "rackpy.raw.engine.Port.get_voltage", "facade": null, "kind": "method",
            "gil_release": false},
 "callback": null, "locks_engine": null, "ownership": "borrowed", "thread": "any",
 "implementation": "native/bindings/engine.cpp",
 "tests": ["tests/bindings/test_port.py::test_get_voltage_reads_each_channel"],
 "docs": ["docs/reference/engine.md#port-voltages"], "notes": null, "reviewer": null}
```

- `id` and `fingerprint` come from the inventory. Never edit them.
- `python.kind` is `no_runtime_meaning` only for a macro that exists just for
  the preprocessor or linker, or for a function Rack declares but never
  defines (notes starting `Declared but never defined`, with the evidence;
  `tests/native/test_sdk_symbols.py` checks that libRack exports no such
  symbol). Such a record has no Python name, and needs a reason in `notes`
  and a documentation anchor.
- Typed stubs for `rackpy.raw` are hand-written next to it
  (`src/rackpy/raw/<namespace>.pyi`); mypy checks the tests against them.
- `callback` (the RP-06 strategy of a virtual method) and `locks_engine` are
  derived from `upstream/callback-rules.json` and `upstream/engine-locks.json`;
  `seed` keeps them in step and the check rejects a mismatch.
- `ownership` and `thread` say what a handle means (borrowed, owned,
  transferred, …) and where it may be used, in words a reviewer can check
  against [the runtime rules](runtime.md).

## Statuses

| Status | Means | The check requires |
| --- | --- | --- |
| `unreviewed` | nobody has looked yet | nothing |
| `planned` | the mapping is designed | a callback strategy other than `unresolved` |
| `implemented` | it is bound | `python.raw` and `python.kind`; an existing implementation file; test node IDs that exist; docs anchors that exist; the Python names present in the **built package**; `gil_release` for engine-locking calls; for an alias, a bound target |
| `verified` | implemented and reviewed | the same, plus a `reviewer` |
| `unrepresentable` | it cannot be mapped faithfully | the reason and the owner's decision in `notes`, and a `reviewer`; it still blocks a release |
| `excluded_internal` | private Rack internals | an entry in `bindings/exclusions.json`, and the inventory's own evidence (`PRIVATE` or an incomplete pimpl type) |

A full-coverage release needs every record `verified` or `excluded_internal`
on each advertised platform. Nothing promotes a record automatically.

## Open owner review items

Some mappings need an owner decision first: a template contract for an
unbounded parameter space ([policy 10](coverage.md#completeness-policy)), or
a scope question. While one is open, every record it affects says so in its
`notes`, in exactly these words, followed by the item:

```text
Open owner review item: the poly* bound of 1 to 32 coefficients.
```

The marker is case-sensitive, and the item runs to the next `.` or `;`. Such
a record can still be `implemented`, and `bin/check_coverage.py check`
passes, but it is a release blocker: `bin/check_coverage.py release` reports
it as `owner-review` even when it is `verified`, and the generated
[coverage report](reference/coverage.md) lists it under *Needs attention*.
Any other mention of an "owner review item" in `notes` (another spelling, or
the marker without an item) fails the check, so an open item cannot sit
where the gate does not read it. When the owner decides, replace the marker
with the decision and its date, for example `owner-approved 2026-10-27`.

## The workflow

1. Claim the family task. Read its records:
   `bin/check_coverage.py report` and the family's page under
   [Reference](reference/index.md).
2. For each declaration you bind, write the adapter in
   `native/bindings/<family>.cpp` as `void bind_<family>(pybind11::module_ m)`.
   Use the runtime's building blocks ([ADR 007](decisions/007-runtime.md)):
   `Borrowed<T>` handles with `identity()`, `requireMainThread`,
   `EngineLease` with the GIL released for engine calls, `PyRef` for stored
   callables, `callPython` for every call from Rack into Python.
3. Write behavioural tests and the documentation, with an anchor per record.
   Tests that need the bindings go in the family's `in_rack/` suite and run
   inside Rack ([ADR 009](decisions/009-values-and-raw-conventions.md)); add a
   case to a compiled C++ reference wherever a result can be compared with
   Rack's own, as `native/tests/values_reference.cpp` does for family 1.
   Follow the naming and value/view conventions of ADR 009.
4. Update the records: status, `python`, `implementation`, `tests`, `docs`.
5. Regenerate and check:

   ```bash
   bin/generate_bindings.py generate
   bin/check_coverage.py check
   uv run pytest tests/coverage
   ```

   The host test `tests/coverage/test_introspection.py` runs the check against
   what the built plugin really exports inside Rack, so a record cannot claim
   a name that is not there.

## When the inventory changes

A new Rack SDK means a new inventory (a decision for the owner, not a version
edit). Then run `bin/generate_bindings.py seed --update`: it adds new
declarations as `unreviewed`, drops removed ones and lists them, and puts every
reviewed record whose declaration changed back to `unreviewed`, noting what it
was. Review each reset record before promoting it again.
