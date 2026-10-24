# RP-15C validation (Linux x86_64)

RP-15C hardens the `json_t` boundary and the patch storage facade. It does
**not** complete RP-15: plugin-wide settings and a real `.vcv` save, reopen,
autosave and undo round trip of Python modules need RP-16 models (RP-15D).

Date 2026-10-24, worktree `/tmp/rackpy-rp15c` on branch
`rp-15c-json-hardening` from `origin/main` 71f3bd6. GCC 16.2.1, uv 0.9.18, the
checksum-verified Rack SDK and Rack Free 2.6.6 (commit 061ccf63), bundled
CPython 3.14.9 and pybind11 3.1.0. Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 4/4 CTests pass |
| `uv run pytest -q -x tests/engine/test_module.py tests/values` | 4 passed; the live-UI module run reports 142 in-Rack tests passed (also in the full run below), including the 8 `test_engine_json_boundary.py` tests and `test_patch_storage.py::test_the_facade_reports_what_rack_ignores` |
| Mutation: `toJsonAt` arrays without `JsonPtr` (leak on error) | `test_conversions_and_their_failures_leak_no_jansson_memory` fails, nothing else; original restored |
| `uv run pytest -q -m 'not dist and not inventory'` | 179 passed, 31 deselected (260 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `prek run --all-files` (ruff, format, strict mypy, planning, callback matrix, manifest) | pass (also after narrowing the `ParamQuantity.to_json` stubs to `dict[str, Any]`; `pytest tests/coverage tests/unit` 73 passed after that) |
| `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass |
| `bin/planning-check`, `bin/generate_bindings.py check`, `git diff --check` | pass |

What the in-Rack tests establish, through Rack's own patch text format:

- Converted `json_t` trees hold exactly one reference per node, and converting
  a borrowed tree leaves its counts unchanged.
- No jansson block is left allocated on the main thread by successful
  conversions, by refusals part-way through a structure (NaN, non-`str` key,
  lone surrogate, 2**64, depth, NUL), by Python-called module and quantity
  JSON, or by Rack's own save and load around contained failures. The
  counting allocator's positive control sees a deliberate one-block leak.
- Rack's loader refuses `\u0000`, so the boundary now refuses NUL in strings.
- Custom data with 64-bit integer extremes, `-0.0`, subnormal and maximal
  floats, escapes and the deepest allowed nesting survives Rack's text.
- Patch data nested beyond 64 levels, or a non-UTF-8 string, does not reach
  `from_json`; it is contained, diagnosed, and the parameters still load.
- Malformed `params` entries follow Rack's rules; unparsable text is refused
  before Rack sees it.
- A `ParamQuantity.to_json` override returning a non-dict raises `TypeError`
  for a Python caller and falls back to Rack's JSON when Rack calls.
- `patch_storage()` raises `OSError` where Rack's
  `createPatchStorageDirectory` silently fails, and leaves the file alone.

## Gates not established

No distributable build, sanitizers, macOS, Windows, performance measurement,
plugin settings, or real `.vcv` save/reopen/autosave/undo of Python modules.
The allocation probe counts jansson blocks only on the calling thread, and it
is a test-hooks build feature, not a production leak check.
