# RP-30A validation (Linux x86_64)

Date 2026-10-26, on `main` after 76e0146. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q tests/services tests/coverage` | 1 + 49 passed |
| `uv run pytest -q -m 'not dist and not inventory'` | 214 passed, 31 deselected (656 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `bin/planning-check` | 45 tasks, acyclic |
| `uv run bin/generate_bindings.py check`, `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

Inside actual Rack (`tests/services/in_rack_utilities/test_utilities.py`):

- **printf.**
  - Signed and unsigned lengths (`%lld`, `%llu`, `%hhd`, `%hu`), floats
    (`%e %g %a`), `%c` from a str, `%%`, and `fv`.
  - Argument count, type and range errors.
  - Refused: `%n`, `*` widths, and NUL in `%s`.
- **Text.**
  - Rack's ASCII-only case, trimming, codepoint truncation and
    ellipsizing, prefixes and suffixes, and positions to locations.
  - Base64, including Rack's skipping of non-Base64 characters.
  - `strcasecmp`, `CaseInsensitiveCompare`, `join`, and `split` (with Rack's
    `Exception` for an empty separator).
- **Time and Unicode.**
  - `format_time`/`format_time_iso`; NaN and 1e300 timestamps are refused.
  - UTF-8 ↔ UTF-32, with Rack's skipping of codepoints past U+10FFFF.
  - Invalid UTF-8 bytes are refused; Rack would hang, as the first test run
    showed.
  - The byte-position functions on valid text, and Rack's no-step on an
    invalid byte.
- **Versions and translations.** Rack's documented version order, parts
  and string form; `get_languages`; `translate` with and without a
  language; and `init` reloading.
- **Random.**
  - Seeded determinism, the unseeded zeros, and setting `state` (`s0 + s1`).
  - `rotl` limits, `min`/`max` and `result_type`.
  - Rack's shared generator and its draws' ranges, `get` by type name,
    `buffer` into a writable buffer (a read-only one is refused),
    `vector`, and `init`.
- **Logging.** `log` with explicit locations, and `WARN` from
  `rackpy.raw.macros`; a bad format raises. The host test reads Rack's
  `log.txt`: `info rackpy_test.py:12 test_logging] rackpy-log marker 5`,
  and the macro's message carries the Python caller's file.

Only Linux x86_64 was tested.
