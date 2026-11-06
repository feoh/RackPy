# ADR 006: Documentation toolchain and executable snippets

- **Status:** Accepted.
- **Date:** 2026-10-22
- **Task:** RP-09
- **Evidence:** [tests/docs](../../tests/docs/test_build.py), [docs/conf.py](../conf.py),
  [docs/_ext/rackpy_docs.py](../_ext/rackpy_docs.py)

## Context

The [documentation plan](../documentation.md) needs warnings-as-errors builds,
checked cross-references, external link checks, and published code that is
tested on a shipped build. RackPy's runtime only exists inside Rack: a docs
build cannot import it, so the API reference has to come from source or stubs,
and runnable snippets have to run in Rack to count as tested.

## Options

| | Sphinx + MyST | MkDocs + mkdocstrings |
| --- | --- | --- |
| Markdown | MyST, which renders the existing `docs/*.md` unchanged | native |
| API pages without importing | sphinx-autoapi parses source and `.pyi` stubs | griffe parses source and stubs |
| Unresolved references as errors | `-W -n` covers pages, anchors and Python cross-references | `--strict` covers pages; anchor validation needs a plugin |
| Custom checks and directives | extensions get the parsed document tree | plugins mostly see Markdown or HTML |
| Link checking | built in (`-b linkcheck`), with timeouts, retries and an ignore list | third-party plugin |
| Maintenance | Sphinx 9.1 is active | Material for MkDocs, the usual theme, has been in maintenance mode since November 2025 and reaches end of life on 2027-05-05; its successor Zensical has no stable release yet |

## Decision

1. **Sphinx 9.1 with MyST-Parser 5.1, sphinx-autoapi 3.8 and the Furo theme**,
   locked in `uv.lock` (dependency group `docs`, installed by default). Furo
   gives keyboard navigation, light and dark schemes that follow the reader's
   setting, and readable contrast.
2. **No import, no Rack.** autoapi documents `src/rackpy` by parsing it. The
   SDK reference pages are generated at build time from
   `upstream/api-surface.json` by a directive, not committed, and say on every
   page that nothing is bound yet. RP-11 replaces that status column with the
   binding manifest.
3. **Warnings are errors.** CI builds with `-W --keep-going -n`. The extension
   `docs/_ext/rackpy_docs.py` adds checks that become errors the same way:
   links to repository files outside `docs/` must exist (they are rewritten to
   GitHub links at the commit being built, so the Markdown works on GitHub and
   on the site), Python listings must be labelled, and images need alt text.
4. **Two kinds of Python listing.** `{rackpy-example}` shows a file from
   `docs/snippets/` verbatim, and that file is what the tests run, so they
   cannot drift. `{rackpy-conceptual}` renders non-runnable code with a visible
   "not runnable, not an available API" label. A plain Python code block is a
   build error.
5. **Snippets run where they claim to.** A `runs-in=rack` snippet runs inside
   Rack Free through the built plugin's trusted entry module, and its standard
   output must equal its `.out` file. `runs-in=python` snippets run with the
   project's Python.
6. **External links** are checked by a separate, scheduled CI job with a
   20-second timeout and two retries, so external sites cannot block pull
   requests. Exceptions are listed with reasons in `docs/conf.py`.
7. **Versions on the site are read from the pins** (`pyproject.toml`,
   `upstream/versions.json`, `packaging/inputs.json`, `uv.lock`) through MyST
   substitutions, not typed into prose.

## Consequences

- Readers can find the development status (site banner and front page),
  installation status, architecture, examples and the reference gaps.
- Tutorials and how-to guides will be added when there is API to teach
  (RP-33 to RP-37); the site does not carry empty placeholders for them.
- Once bindings exist, their stubs feed autoapi; RP-11 decides whether the
  stubs are generated from the manifest or by a stub generator, and must keep
  the build import-free.
- Publishing the site (for example to GitHub Pages) needs owner approval. CI
  keeps the built HTML as a 14-day artifact only.
