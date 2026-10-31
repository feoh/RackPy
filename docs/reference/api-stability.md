# Facade contract and versioning

RackPy is unreleased. The package version is `0.0.0.dev0`, it is marked
`Private :: Do Not Upload`, and no compatibility promise exists between
development versions. This page defines what the Pythonic facade is, the
conventions it follows, and how changes to it are controlled.

## What is public

- **The facade** is every name in `__all__` of a public `rackpy` module
  outside `rackpy.raw`: for example `rackpy.audio`, `rackpy.midi`,
  `rackpy.plugin`, `rackpy.engine.host` and `rackpy.dsp.graph`. Modules and
  packages whose names start with an underscore (such as `rackpy._bootstrap`)
  are private.
- **The raw layer**, `rackpy.raw`, follows Rack's pinned SDK. Its contents and
  status are governed by the [coverage manifest](coverage.md), not by this
  page. The facade never replaces it: see [raw bindings and the facade](mapping.md).

`tests/facade/surface.json` records the facade's names, function and public
method signatures, class bases and re-export origins. `bin/facade-surface
check` compares it with the source and names every added, removed or changed
entry; it runs as a repository hook and as a unit test. A change to the facade
is therefore made deliberately, with `bin/facade-surface write`, and is
visible in review. The snapshot is a change-control tool, not a promise to
users.

## Conventions

These were reviewed across the facade in RP-32:

- Names are snake_case. Operands are positional; options that follow them are
  keyword-only, such as `channel` and `frame` in `rackpy.midi.note_on`,
  `kind` in `open_port`/`open_input`, `cookies` in the network helpers,
  `depth` in `entries`, `level` in `archive`, and the device lists of
  `virtual_driver`.
- Lookups by name raise `LookupError` when nothing matches **or** when several
  things share the name (for example two audio devices called the same);
  selecting by ID stays available.
- JSON coming from Rack, patches or the network is typed as
  `rackpy.types.JSON` and is untrusted: narrow it before use. Values given to
  Rack as JSON are typed `object` and validated at runtime, so ordinary typed
  containers such as `list[str]` are accepted.
- Callbacks are typed with Rack's own event types, for example
  `rackpy.raw.engine.Module.ResetEvent` for `ModuleClass.on_reset`.
- Scoped work uses context managers: `audio.Stream`, `engine.host.undo_step`,
  `Painter.saved` and `Painter.clipped`.
- Access to Rack's context is an explicit call (`engine.host.engine()`,
  which raises `ContextError` outside Rack's live UI), never a property that
  silently reaches for a global. Graph compilation is an explicit
  `Graph.compile()` call.
- Facade helpers delegate to raw bindings. They add argument checks,
  ambiguity checks and Python types, not separate engine behaviour.

## Typing checks

Strict mypy checks `src/`, `tests/`, every documentation snippet
(`docs/snippets/`) and the example plugins (`examples/`). For the snippets alone, `warn_unreachable` is off,
because they demonstrate lifetime transitions (`remove_child`, `close`) that
mypy cannot see change an attribute it has already narrowed. Strict pyright,
pinned in the development dependencies, checks the facade tests, the
snippets, the examples and the facade modules listed in `tests/facade/pyrightconfig.json`,
which include every public facade module. It does not check the raw stubs or
the private underscore modules. `rackpy.dsp.filters.process_block` is typed with overloads
for the time-step and IIR filters; IIR orders other than 3-3 come from the
`IIRFilter[b_order, a_order]` template, which type checkers see as `Any`.

## Proposed release policy

:::{admonition} Proposed, not in effect
:class: warning

This policy needs the owner's approval before the first release (RP-44).
:::

- Versions follow PEP 440. Releases before 1.0 may change the facade in any
  minor release, with each change listed in the release notes.
- From 1.0, a name or signature recorded in `tests/facade/surface.json`
  changes incompatibly only in a major release.
- A deprecated facade name keeps working, with a `DeprecationWarning`, for at
  least one minor release before it is removed.
- The raw layer changes when the pinned Rack version changes, as recorded in
  the coverage manifest; Rack's own deprecations are mirrored there.
