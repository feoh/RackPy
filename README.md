# RackPy

**A planned Python API for VCV Rack: expressive at the surface, native where timing matters.**

RackPy aims to let you build Rack plugins with idiomatic Python while preserving
access to the complete supported public C++ plugin API. It will run **inside Rack**,
not as an external remote-control service.

> **Planning repository — no runtime is implemented yet.**
> There is no installable package, working binding, or completed sample port.
> API designs below are proposals, not promises of available functionality.

## What we are building

- A typed Pythonic interface: properties, enums, named collections, exceptions,
  context managers, and buffer-protocol interoperability where appropriate.
- A comprehensive low-level mirror with explicit C++ → Python mappings.
- Native audio processing, with Python handling configuration and suitable UI work.
- A tested story for ownership, thread affinity, interpreter lifetime, and errors.
- Human-written tutorials and explanations alongside generated API reference.
- Attributed ports of official Rack tutorial/API-guide examples.
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
| Understand the Python/native boundary | [Architecture](docs/architecture.md) |
| See how completeness will be proved | [API coverage contract](docs/coverage.md) |
| Review the planned sample ports | [Example inventory](examples/README.md) |
| Understand performance requirements | [Performance and safety](docs/performance.md) |
| See the documentation experience we intend | [Documentation plan](docs/documentation.md) |
| Pick up implementation work | [Execution plan](planning/README.md) |
| Contribute safely | [Contributing](CONTRIBUTING.md) |

## Decisions

The project owner selected in-process execution, **GPL-3.0-or-later**, and a
repository-and-plan deliverable before implementation. The initial research
baseline is Rack **2.6.6**, commit
`061ccf63c1758599396ac1bb10d47345d9d34076`. Compatibility is not yet tested.
Python versions, binding library, supported architectures, and latency budgets
remain gated decisions; do not infer them from the host used to plan this work.

## Tracking

The detailed, dependency-linked task graph is in Witan and mirrored under
[planning/](planning/README.md). Witan task descriptions contain their own
implementation steps, target files, tests, and acceptance criteria. A Witan
WorkflowProject record could not be created because of a server/schema mismatch;
the [tracking manifest](planning/witan.json) records the recovery requirement.

## License and independence

Original work is [GPL-3.0-or-later](LICENSE). Upstream-derived code must retain its
notices; artwork and documentation have separate provenance checks. No Rack
artwork, logos, fonts, binaries, or SDK are redistributed here.

RackPy is an independent project **for VCV Rack**, not an official VCV product.
The name was changed from VCVPy during planning. Name/package availability and
remaining trademark checks are tracked before release.
