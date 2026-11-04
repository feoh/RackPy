# RackPy execution instructions

## Scope and truthfulness

This repository contains an implementation as well as historical plans. Do not
advertise planned or unverified functionality as tested. Read README.md,
docs/architecture.md, docs/coverage.md, planning/README.md and the relevant task
contract before editing.

Owner decisions: RackPy name; in-process plugin API; GPL-3.0-or-later. External
control is not in this project. Prioritize a usable tool for writing Rack plugins
in Python and iterate on real user needs. Broad public API coverage remains a goal,
but proving every symbol and sample is not a prerequisite for an honest early or
partial release. Private/internal Rack functions are excluded.

## Coordination

Claim the selected Witan task with the real session ID before edits; read its
comments. Check blockers in live Witan, not just the static export. Planning task
IDs map to Witan slugs in planning/witan.json. Never claim all tasks in advance.
The epic and bootstrap retain their original vcvpy-prefixed slugs after rename;
their titles and repository scope are RackPy.

The initial WorkflowProject write failed with a schema mismatch, but the
owner-requested retry succeeded. All tasks are attached to the real project in
planning/witan.json. Join that project at session start; do not recreate it or
duplicate the task graph. RP-01 records the completed metadata recovery.

## Engineering guardrails

- Include only <rack.hpp> in plugin translation units. No PRIVATE calls.
- Pin upstream versions and SDK checksums. Keep downloads/caches outside git.
- No GIL, Python allocation/destruction, logging, or IO on the production audio
  path. RackPy must not introduce additional blocking locks on that path; this
  does not prohibit synchronization already performed by Rack's host or driver
  when using the supported public API. Document host-owned locks separately and
  do not claim the entire host path is lock-free. GIL release alone does not make
  Python real-time safe.
- Rack calls process once per sample; batching must not change cable/feedback
  timing. Engine events are not automatically safe for arbitrary Python callbacks.
- Do not expose borrowed pointers as immortal Python objects or integer addresses.
- Keep raw API coverage and Pythonic convenience separate. Safe adapters do not
  count as completed mappings if public semantics remain inaccessible.
- Unknown/unimplemented symbols and native-only implementations block a
  *full-coverage claim*, not a clearly labeled partial release. Do not weaken
  the coverage denominator or represent unverified functionality as verified.
- Tests using standalone libRack or a fake host do not prove actual Rack loading.
- Treat patch-provided code and pickle as untrusted. Never auto-run scripts from
  patches; this is trusted-user scripting, not a sandbox.
- No Rack/Fundamental visual clones or copied artwork; use original layouts.
- Use uv for Python environments and dependencies. CLI utilities belong in bin/
  and use cyclopts; do not add argparse/click/typer utilities.
- Build docs and samples against the real bindings. Never publish pseudocode as
  an executable example. Proposed interfaces must be labeled as such.

## Validation and handoff

Run repository hooks with prek and checks proportionate to the change, including
actual-Rack tests where host behavior matters. Run the coverage audit when making
a completeness claim or changing its machinery, not as a prerequisite for every
usable partial build. Report skipped platforms and actual-host/manual gates explicitly.
Before status claims re-fetch live state. Do not claim performance without raw
measurements and the exact baseline. Do not publish packages or contact VCV
without owner approval. Leave checkpoints, a commit, validation evidence, and
updated mapping/docs for each completed task.
