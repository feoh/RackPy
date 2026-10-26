# RP-30B validation (Linux x86_64)

Date 2026-10-26, on `main` after 1c595b1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q tests/services tests/coverage` | 2 + 49 passed |
| `uv run pytest -q -m 'not dist and not inventory'` | 215 passed, 31 deselected (650 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `bin/planning-check` | 45 tasks, acyclic |
| `uv run bin/generate_bindings.py check`, `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

Inside actual Rack (`tests/services/in_rack_system/test_system.py`), with
pytest's temporary directories and a loopback HTTP server. No public network
was used.

- **Paths.** Every example in Rack's `system.hpp` header, against Rack's
  actual results: seven differ from the header, and the tests pin Rack's.
  `join`, its variadic form, and `os.PathLike` arguments.
- **Files and directories.**
  - Write and read bytes (with NUL), size, and the kinds of path.
  - Rack's `Exception` for a missing file.
  - Copy, rename, create (with a missing parent failing), symlink, entries
    at depth 0 and -1, remove, and the recursive count.
  - The canonical path through `..`, the absolute path, and the working
    directory (restored), plus the temporary directory and the facade's
    `entries`.
- **Archives.** Directory → bytes → unpack (with Rack's zero-byte
  deletion), a file archive at level 3, and the facade's `unarchive`.
- **`../` entries.** A Zstandard tar with `../escaped.txt`: the facade
  refuses it before writing; Rack's own unarchiver writes outside the
  directory.
- **Time, system and flags.**
  - Monotonic time across `sleep(0.01)`, Unix time, thread time, and a
    negative sleep refused.
  - Core count, Linux OS info, stack trace, and naming a worker thread.
  - FPU flags: reset sets FTZ and DAZ, and they are restored.
  - Blank `open_browser`/`open_directory` do nothing, a shell-breaking URL
    is refused, and `run_process_detached` is refused on Linux.
- **HTTP.**
  - A GET query, with Rack's string-only values.
  - A POST JSON body with a cookie (Rack's trailing `;`), and a DELETE.
  - `None` for a non-JSON body, for a closed port (refused at once) and for
    a malformed URL.
  - A download with the facade's progress callback (values 0 to 1, ending
    at 1.0, and the bytes intact). An HTTP 404 fails, and a raw
    `DownloadProgress` ends at 1.0.
  - `encode_url` and `url_path`.

Only Linux x86_64 was tested. `open_browser` and `open_directory` were tested
only with blank or refused input, so they never launched a desktop
application.
