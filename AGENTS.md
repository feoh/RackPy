# RackPy execution instructions

## Scope and truthfulness

This is currently a planning-only repository. Do not invent an API implementation
or advertise planned functionality as tested. Read README.md, docs/architecture.md,
docs/coverage.md, planning/README.md and your full task contract before editing.

Owner decisions: RackPy name; in-process plugin API; GPL-3.0-or-later. External
control is not in this project. Complete supported public API coverage is the
goal, not an oscillator-only MVP. Private/internal Rack functions are excluded.

## Coordination

Claim the selected Witan task with the real session ID before edits; read its
comments. Check blockers in live Witan, not just the static export. Planning task
IDs map to Witan slugs in planning/witan.json. Never claim all tasks in advance.
The epic and bootstrap retain their original vcvpy-prefixed slugs after rename;
their titles and repository scope are RackPy.

WorkflowProject APIs failed with "WorkflowProject has no property preauthorized".
Do not invent a project/session handle or repair shared infrastructure without
authorization. Follow task RP-01 to attach existing tasks after service repair.

## Engineering guardrails

- Include only <rack.hpp> in plugin translation units. No PRIVATE calls.
- Pin upstream versions and SDK checksums. Keep downloads/caches outside git.
- No GIL, Python allocation/destruction, logging, IO, or blocking locks on the
  production audio path. GIL release alone does not make Python real-time safe.
- Rack calls process once per sample; batching must not change cable/feedback
  timing. Engine events are not automatically safe for arbitrary Python callbacks.
- Do not expose borrowed pointers as immortal Python objects or integer addresses.
- Keep raw API coverage and Pythonic convenience separate. Safe adapters do not
  count as completed mappings if public semantics remain inaccessible.
- Unknown/unimplemented symbols and native-only implementations block a
  full-coverage release. Do not weaken the denominator to make CI green.
- Tests using standalone libRack or a fake host do not prove actual Rack loading.
- Treat patch-provided code and pickle as untrusted. Never auto-run scripts from
  patches; this is trusted-user scripting, not a sandbox.
- No Rack/Fundamental visual clones or copied artwork; use original layouts.
- Use uv for Python environments and dependencies. CLI utilities belong in bin/
  and use cyclopts; do not add argparse/click/typer utilities.
- Build docs and samples against the real bindings. Never publish pseudocode as
  an executable example. Proposed interfaces must be labeled as such.

## Validation and handoff

Run repository hooks with prek; once scaffolded run lint, strict typing,
unit/native/integration tests, coverage audit and docs build as relevant to the
change. Report skipped platforms and actual-host/manual gates explicitly.
Before status claims re-fetch live state. Do not claim performance without raw
measurements and the exact baseline. Do not publish packages or contact VCV
without owner approval. Leave checkpoints, a commit, validation evidence, and
updated mapping/docs for each completed task.
