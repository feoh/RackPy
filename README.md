# RackPy

**A planned Python API for VCV Rack: expressive at the surface, native where timing matters.**

RackPy aims to let you build Rack plugins with idiomatic Python while preserving
access to the complete supported public C++ plugin API. It will run **inside Rack**,
not as an external remote-control service.

> **Early development — partial bindings, not a usable instrument toolkit.**
> The Linux development plugin embeds Python and exposes common values, engine
> components, scoped module construction, built-in port/light/quantity metadata
> adapters, unattached standalone quantities with UI-safe overrides, and
> [detached native widget trees](docs/reference/widget-trees.md) (no UI subclass
> callbacks or scene integration yet).
> See the [coverage report](docs/reference/coverage.md)
> for exact gaps. Production builds register no models; plugin/model factories,
> DSP, lifecycle adapters and the planned instrument examples are not complete.
> There is no published installable runtime bundle. Broader API designs remain proposals.

## What we are building

- A typed Pythonic interface: properties, enums, named collections, exceptions,
  context managers, and buffer-protocol interoperability where appropriate.
- A comprehensive low-level mirror with explicit C++ → Python mappings.
- Native audio processing, with Python handling configuration and suitable UI work.
- A tested story for ownership, thread affinity, interpreter lifetime, and errors.
- Human-written tutorials and explanations alongside generated API reference.
- Original, runnable examples for every feature the official Rack tutorial,
  API guide and plugin template demonstrate.
- Reproducible Linux, macOS, and Windows packages, subject to feasibility gates.

**“Complete” is a release criterion, not the current state.** It means the pinned
public SDK surface has no unreviewed or unimplemented Python mappings. Private
Rack internals are not part of the supported plugin API. Templates, macros,
platform-specific APIs, and third-party types need explicit semantic mappings;
they must not disappear from the coverage denominator.

## Start here

| You want to… | Read |
| --- | --- |
| Understand scope and upstream evidence | [Scope and research](docs/scope.md) |
| See the pinned SDK, toolchain and platform targets | [Compatibility baseline](docs/compatibility.md) |
| Check licenses, assets and sample provenance | [Licensing](docs/licensing.md) |
| Understand the Python/native boundary | [Architecture](docs/architecture.md) |
| See how completeness will be proved | [API coverage contract](docs/coverage.md) |
| Review the planned examples | [Example inventory](examples/README.md) |
| Understand performance requirements | [Performance and safety](docs/performance.md) |
| See the documentation experience we intend | [Documentation plan](docs/documentation.md); build the site with `uv run sphinx-build -W -n -b html docs build/docs/html` |
| Pick up implementation work | [Execution plan](planning/README.md) |
| Build, test and run CI checks locally | [Contributing](CONTRIBUTING.md) and [ADR 005](docs/decisions/005-build-and-ci.md) |

## Decisions

The project owner selected in-process execution, **GPL-3.0-or-later**, and a
repository-and-plan deliverable before implementation. The baseline is Rack
**2.6.6**, commit `061ccf63c1758599396ac1bb10d47345d9d34076`. RP-02 rechecked
it and pinned the SDK archives by SHA-256
([compatibility baseline](docs/compatibility.md)). RP-04 chose a bundled
CPython 3.14 and pybind11 3.1.0 ([ADR 002](docs/decisions/002-embedding-and-bindings.md));
RP-07 set the working performance budgets ([ADR 004](docs/decisions/004-performance-budgets.md)).
The RP-08 skeleton loads into Rack Free 2.6.6 on Linux x86_64. macOS and
Windows have not been built or run.

## Tracking

The detailed, dependency-linked task graph is in Witan and mirrored under
[planning/](planning/README.md). Witan task descriptions contain their own
implementation steps, target files, tests, and acceptance criteria. The Witan
project is `wp-rackpy-pythonic-full-surface-bindings-for-vcv-ra-7a9779`.
An initial project-write schema error was resolved on retry; the
[tracking manifest](planning/witan.json) records the project and all task slugs.

## License and independence

Original work is [GPL-3.0-or-later](LICENSE). Upstream-derived code must retain its
notices; artwork and documentation have separate provenance checks. No Rack
artwork, logos, fonts, binaries, or SDK are redistributed here.

RackPy is an independent project **for VCV Rack**, not an official VCV product.
The name was changed from VCVPy during planning. The terms, the asset ledger,
sample provenance and the name checks are in [licensing](docs/licensing.md).
RackPy's examples are original code, not adaptations of the VCV manual
([ADR 001](docs/decisions/001-licensing-and-identity.md)).
