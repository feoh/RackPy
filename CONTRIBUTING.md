# Contributing

Start with [the task graph](planning/README.md). There is intentionally no
`uv sync`, build, or install quickstart for an implementation that does not yet
exist.

## Before changing code

1. Read the architecture, coverage, and performance contracts.
2. Check `git status` and `git worktree list`; do not overwrite someone else's work.
3. Read the live Witan task and comments, verify its prerequisites, and claim it.
4. Use a separate worktree for branch experiments. Do not stash another person's work.
5. Keep a change bounded to its acceptance criteria.

## Definition of done for future implementation tasks

- The real supported SDK compiles the change, with no internal API access.
- Normal use, bad inputs, stale handles, thread violations, and failure paths have tests.
- Symbol mappings link to Python names, implementation, docs, and behavioral tests.
- New facade behavior has type hints, a narrative explanation, and runnable examples.
- C++ callbacks cannot leak exceptions into Rack; production DSP remains Python-free.
- Tests and docs use the same package that will be shipped, not a hidden mock.
- Relevant validation passes locally and in CI; unavailable platform tests remain
  explicit blockers, not successful checks.
- Upstream adaptations retain copyright, source revision, license, and changes.
- Record the exact commands and resulting artifacts in the Witan checkpoint.

## Planning-repository checks

Use `prek run --all-files`. The repository currently has no Python package,
so mypy, runtime tests, and Rack integration are not yet applicable. Future
tooling and those checks are work in the execution plan.

## Releases and security

Release publication, signing credentials, official library submission, and
outbound licensing/trademark inquiries require owner approval. Please do not
put credentials, Rack Pro binaries, SDK archives, or personal patches into git.
Report security concerns privately to the repository owner rather than attaching
a malicious executable patch to a public issue.
