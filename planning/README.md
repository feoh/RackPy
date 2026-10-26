# RackPy execution plan

This is a specification and task graph, not a completed implementation.

## Durable records

- [Detailed human-readable task contracts](TASKS.md)
- [Machine-readable task contracts and dependency DAG](tasks.json)
- [Witan slugs and project-recovery state](witan.json)
- [RP-14 bounded implementation slices](rp-14-slices.md) (supplements the original graph)
- [RP-15 bounded implementation slices](rp-15-slices.md) (supplements the original graph)
- [RP-16 bounded implementation slices](rp-16-slices.md) (supplements the original graph)
- [RP-17 bounded implementation slices](rp-17-slices.md) (supplements the original graph)
- [RP-25 bounded implementation slices](rp-25-slices.md) (supplements the original graph)
- [RP-26 bounded implementation slices](rp-26-slices.md) (supplements the original graph)

Witan is authoritative for live task status, ownership, comments and blockers.
Git holds a durable copy of task scope; status is deliberately not frozen into
tasks.json. Read both before working. Stable IDs RP-00 through RP-44 map to real
Witan task slugs. The epic and RP-00 retain vcvpy-prefixed historical slugs;
their titles/repository scope were changed to RackPy.

## Delivery phases

| Phase | Tasks | Exit gate |
| --- | --- | --- |
| Planning | RP-00 | Public repo, researched contracts, complete Witan task graph |
| Tracking recovery | RP-01 | Actual WorkflowProject attached without duplicated tasks |
| Scope/legal/feasibility | RP-02–RP-07 | Pinned public SDK, lawful sources, proven embedding/threading/performance |
| Foundations | RP-08–RP-17 | Reproducible build, docs pipeline, manifest, ownership, engine/module/plugin foundations |
| API families | RP-18–RP-30 | DSP/SIMD, native graph, callback laboratory, expanders, engine, UI/rendering, IO and services |
| Completeness and ergonomics | RP-31–RP-32 | No missing public family; coherent typed facade |
| Samples and manual | RP-33–RP-37 | All official source blocks accounted for; working ports and tested prose |
| Hardening and packaging | RP-38–RP-41 | Safe behavior and actual Linux/macOS/Windows candidate validation |
| Proof and release | RP-42–RP-44 | Exact surface/sample parity, measured budgets, owner-approved publication |

RP-01 is independent metadata recovery; it does not block SDK discovery.
RP-02 is the first implementation-planning prerequisite. RP-05 inventory work can
overlap the embedding/lifetime feasibility work after the SDK is pinned.
Production scaffold RP-08 waits for feasibility decisions, not just a hello-world
binding. Exact dependencies are in tasks.json and mirrored in Witan.

## Definition of task quality

Every contract specifies deliverables, ordered steps, validation and acceptance.
All implementation tasks inherit these requirements:

1. Claim the live task and read comments; do not work through an open blocker.
2. Read the source API and the architecture/coverage/performance contracts.
3. Implement a bounded slice with explicit error, ownership and thread semantics.
4. Update manifest IDs, strict types, tests and human documentation together.
5. Validate using the real SDK; use actual Rack where host behavior matters.
6. Leave an immutable commit and checkpoint with exact commands/results.
7. Close only after acceptance criteria pass; label unsupported platforms honestly.

If a family is too large for one model context, split it into manifest-ID-bounded
child tasks with the same proof obligations, and block its parent on them. A family
task is not permission to leave most overloads for an untracked future effort.

## Mandatory decision gates

- **SDK scope:** preserve the complete supported public plugin API goal. Private
  internals are not supported API; difficult public symbols are not internals.
- **Legal:** capture manual snippet adaptation terms and keep artwork separate.
- **Embedding:** real host loading, runtime ownership and toolchain compatibility.
- **Timing:** no Python on the production audio path; synchronous callback
  semantics still need a genuine non-real-time Python execution domain.
- **Generics:** a finite convenient subset is not automatically full template
  coverage. Record meaningful generic/extension mappings or request scope review.
- **Platforms:** execution on a native target, not cross-compilation, establishes support.
- **Release:** publication/signing/library submission requires owner approval.

## Non-goals

External remote control, binding arbitrary third-party plugin internals, porting
all Fundamental modules, a general Python-to-native DSP compiler, untrusted-code
sandboxing, Rack Pro/DAW embedding support without explicit testing, and
redistributing the Rack SDK or restricted graphics.

## Owner amendments

**2026-10-21: examples, not manual ports.** In RP-33 to RP-36 and RP-42, "port"
now means an *original* RackPy example that demonstrates the same feature as
an official tutorial, API guide or template sample, linked to its source
section. RackPy does not copy or adapt the VCV manual's code or text, so the
manual's unclear license does not gate these tasks. The `helper.py` plugin
template may still be derived from GPL Rack source with attribution. Coverage
of every sample's feature is still required; see
[ADR 001](../docs/decisions/001-licensing-and-identity.md). The task contracts
below keep their original wording; this amendment and the Witan comments on
those tasks take precedence.

## Witan project and recovery history

The initial WorkflowProject search/create returned:
`WorkflowProject has no property preauthorized (HTTP 400, bad_request)`.
After the repository and task graph were published, the owner-requested retry
succeeded. The project is
`wp-rackpy-pythonic-full-surface-bindings-for-vcv-ra-7a9779`; its epic and all 45
child tasks were attached and read back without duplication or dependency changes.
No shared infrastructure was modified by this session.

The [tracking manifest](witan.json) records real returned handles. RP-01 retains
the recovery contract and its live resolution in Witan. Future sessions should
join the existing project, not recreate it or its tasks.
