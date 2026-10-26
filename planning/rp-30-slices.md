# RP-30 bounded implementation slices

RP-30 (`tk-rackpy-30-bind-public-services-settings-assets-f-f47a4f`) binds
Rack's services. It is split into bounded children, each closed with a
commit on `main`. Decisions follow the owner's standing instruction to take
the recommended option.

| Slice | Scope |
| --- | --- |
| 30A | `string.hpp`, `random.hpp`, `logger.hpp` and the logging macros (family 1) |
| 30B | `system.hpp` and `network.hpp` (family 8): filesystem, time, processes, HTTP with loopback fixtures |
| 30C | `settings.hpp` (with `preferDarkPanels` re-rendering, as RP-27 asked), `mutex.hpp` and `weakptr.hpp` |

**RP-30A — string, random and logger utilities**
(`tk-rackpy-30a-string-random-and-logger-utilities-eb984d`). 87 records.
Validation: [rp-30a-validation.md](rp-30a-validation.md).

- **printf.** `string::f`/`fV` and `logger::log` are printf-variadic.
  `services::formatPrintf` parses the format and checks each conversion
  against its Python argument. It converts the argument to the C type that
  the conversion and its length modifier name, and calls Rack's
  `string::f` on that single conversion. `%n`, `%p` and `*` are refused.
  `logger::log` gets the result as one `%s` argument.
- **Rack 2.6.6 findings.**
  - `UTF8toUTF32` loops forever on invalid UTF-8 (`continue` without
    advancing), so invalid bytes are refused.
  - `fromBase64` never raises, though its header says it does.
  - `formatTime` passes `strftime` a null `tm` for out-of-range timestamps,
    so they are refused.
  - `Xoroshiro128Plus::rotl(x, 0)` shifts by 64, so `k` must be 1 to 63.
- **Rack's generator.** `random::local()` is one process-wide generator,
  shared without a lock; Python's draws race with Rack's own, as documented.
- **Variables and macros.** `logger::logPath` becomes
  `get_log_path`/`set_log_path`. The macros DEBUG/INFO/WARN/FATAL are
  Python functions in `rackpy.raw.macros` that pass the caller's frame.
- **Introspection.** The checker now records `__lt__` and `__call__`
  (operator mappings).

**RP-30B — system and network services**
(`tk-rackpy-30b-system-and-network-services-2efdfb`). 54 records, plus the
`rackpy.services` facade (the deliverable package). Validation:
[rp-30b-validation.md](rp-30b-validation.md).

- **Calls.** Paths are str or `os.PathLike`; blocking calls release the
  GIL.
- **Rack 2.6.6 guards.**
  - `runProcessDetached` asserts off Windows, so it is refused on Linux and
    macOS.
  - `openBrowser`/`openDirectory` pass the string to `xdg-open` inside
    shell double quotes, so `"`, `\`, `$` and `` ` `` are refused.
  - `unarchiveToDirectory` follows `../` entries (tested); the facade's
    `unarchive` checks entries with Python 3.14's `tarfile` (Zstandard)
    first.
- **Rack 2.6.6 findings.** Several header examples for paths differ from
  Rack's results (`std::filesystem`): `join("/a", "")`, `getDirectory("/")`,
  `getFilename("/foo/bar/")` and `getFilename("/")`, `getStem("/foo/.bar")`,
  and `getExtension` of `"/foo/.."` and `"/foo/.hidden"`. Tests pin Rack's
  results, and the docs list them. Rack's cookie header ends with `;`.
- **Network.** `request_json`/`request_download` block in curl (30 s to
  connect). A `DownloadProgress` lets another thread watch a download, and
  the facade's `download(on_progress)` uses it.

**RP-30C — settings, mutex and weak pointers**
(`tk-rackpy-30c-settings-mutex-and-weak-pointers-bfa22a`). 109 records;
families 1 and 8 have none left. Validation:
[rp-30c-validation.md](rp-30c-validation.md).

- **Settings.** One property per global on a `settings` object, used on
  the main thread only, as Rack's UI does. `headless` and `is_plugin` are
  read-only. Float settings must be finite, and `thread_count` must be at
  least 1. `pluginSettingsJ`, `moduleInfos` and `moduleWhitelist` are
  copies. `get_module_info` returns a view that is found again by its slugs
  each time it is used. A `prefer_dark_panels` change re-renders a
  `ThemedSvgPanel` at its next `step()`, as RP-27 asked (tested).
- **Mutex.** Rack's `SharedMutex` asserts on pthread errors, so RackPy
  records the threads that hold it and refuses relocking or a foreign
  unlock. Waiting releases the GIL. A mutex dropped while held is leaked
  (a held rwlock must not be destroyed). `SharedLock` is RAII made
  explicit: `release()` or the end of its `with` block. Its native object
  cannot be copied, so the lock is released exactly once.
- **Weak pointers.** `WeakBase` is `Widget`'s base in the closure, so
  `WeakPtr` is for widgets; `WeakBase`'s members are on `Widget` and
  `HostWidget`. Rack's `rwlock` field has no runtime meaning in Python.
