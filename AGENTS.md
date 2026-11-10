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

## Wall-clock time is paramount

Owner instruction, 2026-11-07. Minimizing elapsed time to a finished, honest
result matters more than minimizing tokens, sessions or agent count. Use
subagents and every other parallelization tool wherever it will finish the
work sooner:

- Split independent work (review chunks, per-platform checks, separate files,
  separate tasks with no shared blocker) across parallel subagents by default.
  Serialize only where a real dependency or a shared resource (one Rack host
  lock, one CI run, one branch head) forces it.
- Never wait synchronously on CI, builds or long tests. Start them, work on
  something independent, and have an async subagent report completion.
- Make independent tool calls in one batch instead of one at a time.
- When a task is too large for one session, split it into claimable child
  tasks that different agents can run at once, rather than running it alone
  for days.
- Parallelism does not change the evidence bar: every claim still needs its
  own verified result, and the merge of parallel work still runs the hooks.

See the global instructions for subagent mechanics (claude-bridge children run
with `async: true`).

## CI is the last check, not the first

Owner request, 2026-11-10 (RP-56). Of the 40 CI runs before it, 23 failed: seven
only on one line of trailing whitespace on `main`, nine with a test that failed on
Linux or on several platforms. A full run took a median of 59 minutes
(planning/rp-56-validation.md).

- Before every push, run the hooks (`prek run --all-files`; a clone with
  `prek install` does it on commit and push) and the affected tests in a local
  Rack. Never push with `--no-verify`. A red `main` fails every branch cut from it.
- Dispatch the smallest run that answers the question (CONTRIBUTING.md, CI):
  `-f fast=true` for a cross-platform smoke; a plain dispatch for every test
  suite; `-f candidates=true` only when the tests already pass and you need
  release evidence or candidate packages. Narrow one platform with
  `macos_tests` or `windows_tests`.
- Never wait for a run's conclusion. Run `bin/ci-failures --wait failure` in an
  async subagent and act on the first failed job.
- A test that failed on Linux, or on two or more platforms, is a bug: reproduce
  it locally and fix it before another run. A test that failed on one of macOS
  or Windows only may be intermittent: `gh run rerun RUN --failed` once, and
  record the run and job on a Witan task either way. Do not add retries to tests.
- One repeat exists, by owner decision (2026-11-10, RP-57): the macOS release
  check runs Rack once more when Rack died in its own MIDI start-up before
  loading a plugin, and counts it in the summary. Do not widen it to other
  failures or copy it elsewhere without the owner.

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
