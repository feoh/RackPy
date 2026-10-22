# Documentation conventions

The site is Sphinx with MyST Markdown, built with warnings as errors. These
rules keep it honest: nothing on it may claim to work unless a test shows it.

## Build and check

```bash
uv run sphinx-build -W --keep-going -n -b html docs build/docs/html   # the site
uv run pytest -m docs                                                 # snippets and checks
uv run sphinx-build -b linkcheck docs build/docs/linkcheck            # external links (network)
```

The first command fails on any warning: an unresolved page, anchor or
cross-reference; a page in no table of contents; a link to a repository file
that does not exist; an unlabelled Python listing; an image without alt text.
The external link check is a separate CI job that runs weekly and on demand,
with a 20-second timeout and two retries per link, so a slow website cannot
block a pull request. Its reviewed exceptions are in `docs/conf.py`.

Nothing in the build imports `rackpy` or needs Rack. The Python API pages come
from [sphinx-autoapi](https://sphinx-autoapi.readthedocs.io/en/latest/), which parses
`src/`, and the SDK tables come from `upstream/api-surface.json`.

## Code listings

Every Python listing is one of two kinds, and the build rejects anything else.

**A runnable example** is a file in `docs/snippets/`, shown with:

````markdown
```{rackpy-example} rack_runtime.py
:caption: What the reader sees above the code
```
````

The page shows the file itself (without its licence and test header), so the
displayed code and the tested code cannot drift. The file's header says where
it runs, and optionally which file holds its exact expected output:

```text
# rackpy-example: runs-in=rack output=rack_runtime.out
```

`runs-in=rack` snippets run **inside Rack**, in the built plugin, through the
host test harness (`tests/docs/test_snippets.py`, marked `host`).
`runs-in=python` snippets run with the project's Python and must not need
Rack. A snippet that is not shown on any page fails the tests, as does a page
that shows a snippet that does not exist.

**A conceptual sketch** is code that does not run: a proposed interface, or a
fragment that only makes sense in context. Use:

````markdown
```{rackpy-conceptual}
:caption: Proposed: a module with one gain parameter
class Gain(rackpy.Module): ...
```
````

It renders with the label "not runnable, not an available API". Proposed
interfaces must always use this, never a plain code block.

Shell commands, JSON and C++ listings are not run; use `bash`, `json`, `cpp`
or `text` fences for them. A shell command on the site must be one a reader can
run in a checkout.

## Links

- Link to other pages with relative Markdown links (`[ADR 002](decisions/002-embedding-and-bindings.md)`).
  Anchors are heading slugs, as on GitHub, and are checked.
- Link to files outside `docs/` the same way (`[pins](../upstream/versions.json)`).
  The build turns these into links to the file on GitHub at the commit being
  built, and fails if the file does not exist. The same Markdown therefore
  works on GitHub and on the site.
- Do not hard-code version numbers that live in pinned files. Use the
  substitutions `{{rackpy_version}}`, `{{rack_version}}`, `{{rack_commit}}`,
  `{{python_version}}` and `{{pybind11_version}}`, which `docs/conf.py` reads
  from `pyproject.toml`, `upstream/versions.json`, `packaging/inputs.json` and
  `uv.lock`.

## Accessibility and accuracy

- Every image needs alt text, and every diagram a text equivalent. The build
  rejects images without alt text.
- Never use colour alone to carry meaning.
- No screenshots, outputs or benchmark numbers that were not produced by the
  code they describe. Measurements name their machine and command, as ADR 004
  and ADR 005 do.
- Generated pages say so at the top and are never edited by hand.
