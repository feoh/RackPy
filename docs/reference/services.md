# Services

Rack's services: text, random numbers and the log (**RP-30A**), files and
HTTP (**RP-30B**), and settings, Rack's shared mutex and weak pointers
(**RP-30C**). **Implemented, Linux x86_64 inside Rack Free 2.6.6:**
`rackpy.raw.string`, `rackpy.raw.random`, `rackpy.raw.logger`, the logging
macros, `rackpy.raw.system`, `rackpy.raw.network`, `rackpy.raw.settings`,
`rackpy.raw.SharedMutex`/`SharedLock` and `rackpy.raw.WeakPtr`, with the
`rackpy.services` facade (see the [coverage report](coverage.md)).

(services-string)=
## Text: `rackpy.raw.string`

Rack's `string::f` is printf, a variadic C call whose argument types its
format decides. `f(format, *args)` reads the format, checks each conversion
against its Python argument, converts the argument to the C type the
conversion and its length modifier name (`%d` an `int`, `%lld` a
`long long`, `%u` an `unsigned`, `%f` a `double`, `%s` a str or bytes, `%c` an
int or a one-character str), and formats it with Rack's `string::f`.
`%n`, `%p` and `*` widths are refused. Too few or too many arguments, a
wrong type or a value the C type cannot hold raises. `fv(format, args)` is
`string::fV`, with a sequence for C's `va_list`.

The rest are Rack's functions under snake-case names:

- **Case and trimming.** `lowercase` and `uppercase` change ASCII letters
  only, and `trim` removes surrounding whitespace.
- **Cutting to length.** `truncate`, `truncate_prefix`, `ellipsize` and
  `ellipsize_prefix` count codepoints.
- **Comparing.** `starts_with`, `ends_with`, `strcasecmp` and
  `CaseInsensitiveCompare` (a less-than).
- **Joining and splitting.** `join(container, seperator)`, with Rack's
  spelling, and `split(s, seperator, max_tokens)`, which raises Rack's
  `Exception` for an empty separator.
- **Positions.** `position_to_location` and `location_to_position` convert
  between a byte position and a `Location` (line and column, from 0).
- **Base64.** `to_base64` takes bytes. `from_base64` returns bytes and
  skips characters that are not Base64; Rack's header says it raises, but
  Rack 2.6.6 never does.
- **Time.** `format_time(format, timestamp)` is `strftime` of the local
  time, and `format_time_iso`. An out-of-range timestamp raises
  `ValueError`, where Rack would pass `strftime` a null time.
- **UTF-8.** Positions are byte positions in the UTF-8 text (a str is
  encoded as UTF-8; bytes are used as they are). `utf8_to_utf32` returns
  codepoints, and `utf32_to_utf8` returns bytes, skipping codepoints past
  U+10FFFF. Rack 2.6.6's `UTF8toUTF32` never returns on invalid UTF-8,
  because it skips an invalid sequence without advancing, so invalid bytes
  raise `ValueError`. `utf8_next_codepoint`, `utf8_prev_codepoint`,
  `utf8_length`, `utf8_codepoint_index` and `utf8_codepoint_pos` stop at
  an invalid byte, as Rack's do.
- **Versions.** `Version` is Rack's version: dotted `parts`, compared part
  by part with integers as numbers (`1.a.0 < 1.0.0 < 1.10.0`), with
  `get_major`, `get_minor` and `get_revision`.
- **Translations.** `translate(id)`, in Rack's language, else English, else
  empty; `translate(id, language)`; `get_languages()`; and `init()`, which
  reloads Rack's translation files. All run on the main thread.

(services-random)=
## Random numbers: `rackpy.raw.random`

`Xoroshiro128Plus` is Rack's generator, as a value. It is unseeded at first
(it returns zeros), or seeded with `Xoroshiro128Plus(s0, s1=0)` or
`seed()`, which draw one number as Rack's do. Calling it returns the next
64-bit number, `state` is its two state words, and `is_seeded()` reports
whether those words are nonzero. `result_type` is Python `int`. `rotl(x, k)` needs `k`
from 1 to 63: Rack's shift by 64 for `k` 0 is undefined in C++.

`local()` is Rack's own generator: one for the whole process, kept until
Rack quits. Rack's engine threads and UI use it without a lock, so
Python's draws race with theirs, as Rack's own do. `init()` reseeds it, and
`u64`, `u32`, `uniform` (`[0, 1)`), `normal`, `get(type)`, `get_uint32`
and the other `get_*`, `buffer(out)` (it fills a writable byte buffer) and
`vector(len)` draw from it. For reproducible numbers, make a generator of
your own.

`get(type)` is Rack's `get<T>`, with `T` named as a string: Rack's
specializations `uint64` (the default), `uint32`, `uint16`, `uint8` (the top
bits), `bool`, `float` and `double`, and, through Rack's primary template,
`int64`, `int32`, `int16` and `int8`: the next 64-bit number converted to the
signed type, so its low bits, as GCC converts. Any other name raises
`ValueError`. Seeded draws of every type match Rack's own (the compiled
reference in `tests/values`).

(services-logger)=
## The log: `rackpy.raw.logger`

`log(level, filename, line, func, format, *args)` is Rack's log. It formats
the message as `string.f` does, then passes it to Rack's log as one `%s`
argument: the same text, without Python's format string ever reaching
Rack's printf. It is thread-safe and releases the GIL. `Level` has Rack's
levels, `DEBUG_LEVEL`, `INFO_LEVEL`, `WARN_LEVEL` and `FATAL_LEVEL`, which
Rack's log labels `debug`, `info`, `warn` and `fatal`; it writes every
level. `get_log_path()` returns the file Rack logs to, and
`set_log_path()` sets Rack's variable, though Rack opened its log at
start-up, so later messages still go there.

`rackpy.raw.macros.DEBUG`, `INFO`, `WARN` and `FATAL` are Rack's macros.
Each logs at its level with the Python caller's file, line and function, as
the macros do with `__FILE__`, `__LINE__` and `__FUNCTION__`. As in Rack,
`FATAL` does not stop anything.

(services-system)=
## Files, time and processes: `rackpy.raw.system`

Paths are str or `os.PathLike` and come back as str. Calls that touch the
disk or sleep release the GIL. They block the calling thread, which never
runs on Rack's audio path; on the main thread, Rack's window waits.

- **Paths.** `join(path1, path2, *paths)` (Rack's variadic template),
  `get_directory`, `get_filename`, `get_stem` and `get_extension`,
  `get_absolute`, and `get_canonical` (the path must exist). Rack 2.6.6
  follows `std::filesystem`, which differs from some examples in its header:
  - `join("/a", "")` gives `"/a/"`;
  - `get_directory("/")` gives `"/"`;
  - `get_filename("/foo/bar/")` and `get_filename("/")` give `""`;
  - `get_stem("/foo/.bar")` gives `".bar"`;
  - `get_extension("/foo/.hidden")` gives `""`, and for `"/foo/.."` gives
    `"."`.

  RackPy keeps Rack's results.
- **Files and directories.** `exists`, `is_file`, `is_directory`,
  `get_file_size`, `get_entries(dir, depth)` (`-1` recurses all the way),
  `rename`, `copy`, `create_directory`, `create_directories`,
  `create_symbolic_link`, `remove`, `remove_recursively` (returns a count),
  `read_file` (bytes) and `write_file`. Most return whether they
  succeeded; `read_file` and `write_file` raise Rack's `Exception`.
  `get_working_directory`, `set_working_directory` (the whole process's)
  and `get_temp_directory`.
- **Archives.** `archive_directory(archive_path, dir_path, level)` writes a
  `.tar.zst`, `archive_directory_to_bytes` returns one, and
  `unarchive_to_directory(archive, dir_path)` unpacks a path or bytes. As
  Rack documents, a zero-byte file in an archive deletes the existing file.
  Rack 2.6.6 refuses absolute entry paths but follows `../` entries out of
  the directory, so unpack only archives you trust;
  `rackpy.services.system.unarchive` checks the entries first.
- **Time.** `get_time` (monotonic, for measuring), `get_unix_time`,
  `get_thread_time` and `sleep(seconds)`.
- **System.** `get_logical_core_count`, `get_operating_system_info`,
  `get_stack_trace` and `set_thread_name` (the calling thread's).
- **FPU flags.** `get_fpu_flags`, `set_fpu_flags` and `reset_fpu_flags`
  act on the calling thread's floating-point flags; `reset` sets
  flush-to-zero and denormals-are-zero, as Rack's engine threads run, and
  Python's own arithmetic on that thread follows them.
- **Opening things.** `open_browser(url)` and `open_directory(path)` open
  the desktop's handler without blocking. Rack runs `xdg-open "<string>"`
  through the shell, so RackPy refuses `"`, `\`, `$` and `` ` ``, which
  would run shell code. `run_process_detached` is implemented by Rack on
  Windows only and asserts elsewhere, so RackPy refuses it on Linux and
  macOS.

(services-network)=
## HTTP: `rackpy.raw.network`

`request_json(method, url, data, cookies)` requests a JSON API.
`request_download(url, filename, progress, cookies)` downloads a file, and
another thread can read its progress (0 to 1) from a `DownloadProgress`
meanwhile. Both block in curl, which allows 30 seconds to connect and sets
no overall limit, with the GIL released.

- `Method` has `METHOD_GET`, `METHOD_POST`, `METHOD_PUT` and
  `METHOD_DELETE`. In Rack 2.6.6, `METHOD_PUT` with JSON data actually sends
  POST because of Rack's curl options; without data it can read stdin. Do not
  use it for PUT. The `services.network.put_json` facade raises
  `NotImplementedError` rather than sending an unintended request.
- `request_json` sends `data` as the query for `METHOD_GET` (string values
  only, as Rack does) or as the JSON body otherwise. It returns the parsed
  answer, or `None` on any failure: no connection, a malformed URL, or a
  body that is not JSON.
- Cookies are a dict (`CookieMap`), and Rack's cookie header ends with `;`.
- `request_download` returns whether it succeeded; an HTTP error fails.
- `encode_url(s)` URL-encodes a string, and `url_path(url)` returns a
  URL's path.

(services-settings)=
## Settings: `rackpy.raw.settings`

Rack keeps its settings in globals, which its UI reads and writes on the
main thread. `rackpy.raw.settings.settings` has one property for each of
them, such as `prefer_dark_panels`, `rack_brightness`, `cable_colors`,
`knob_mode` and `thread_count`. The properties work on the main thread only.
Rack writes the settings to `settings_path` when it quits.

Each property reads and writes the Rack global of the same name in
snake case (`cable_opacity` is `settings::cableOpacity`), except
`plugin_settings`, which is `pluginSettingsJ`. A test in Rack
writes a new value to every property and reads Rack's global from C++, and
the reverse, so it would fail if a property reached the wrong global.

- **Read-only.** Rack sets `headless` and `is_plugin` at start-up and relies
  on them afterwards, so they cannot be changed.
- **Checks.** Float settings must be finite, and `thread_count` must be at
  least 1. Rack's engine reads `thread_count` at each block.
- **Copies.** `plugin_settings` (Rack's `pluginSettingsJ`) is Python data;
  assign it to replace Rack's. `module_infos` and `module_whitelist` are
  copies too, and assigning one replaces Rack's whole mapping. Reading
  `cable_colors`, `cable_labels` or `recent_patch_paths` returns a list copy;
  changing the list or its colors does not change Rack. Assign a list back
  to replace the setting. `PluginWhitelist.module_slugs` similarly returns
  a set copy; assign a set to replace it.
- **One module.** `get_module_info(plugin_slug, module_slug)` returns a
  `ModuleInfoView`, which writes straight to Rack's entry, or `None`.
  `is_module_whitelisted` and `reset_cables` are Rack's functions.
- **Re-rendering.** Rack's themed widgets (`ThemedSvgPanel`, and the themed
  ports and screws) pick their SVG from `prefer_dark_panels` in `step()`.
  So changing the setting re-renders them at the next frame, and their
  framebuffers are marked dirty. This is tested in Rack.
- **Not refreshed.** Writing `ui_theme` alone does not refresh the UI;
  Rack's View menu also refreshes the theme.
- **The login token.** `token` is the user's VCV login token. Never log it.
  Rack's library client sends it to VCV's server as a cookie.

Some settings take effect only at start-up, or at quit, so writing them in
a running Rack does less, or more, than it seems:

- **`restart`.** When Rack quits with it set, it starts again (Rack's menus
  set it after a language change or a library update). Rack does not
  save it.
- **`safe_mode`.** Rack reads it at start-up, when it skips loading plugins
  and the autosave. Its patch manager also reads it as Rack quits: set, it
  deletes the autosave instead of saving it. Rack always saves it as `false`.
- **`dev_mode`.** Rack chooses its directories and plugin folder from it at
  start-up; later, it changes only what Rack's menus show and whether
  Rack checks for updates. Rack does not save it.
- **`verify_https_certs`.** Rack's HTTP client reads it at every request,
  Rack's own and `rackpy.raw.network`'s. `false` turns off certificate
  checks for every HTTPS request, which lets anyone on the network forge
  the replies. Rack saves it.
- **`window_pos`, `window_size`, `window_maximized`.** Rack's window
  writes them as it moves and resizes, and reads them only when it opens.
  Writing them does not move the window, and Rack overwrites them at the
  window's next move. Rack's default `window_pos` is NaN, NaN: no saved
  position.
- **`settings_path`.** Where Rack saves the settings when it quits.
- **`sample_rate`.** 0 means the audio device's rate. Rack's engine reads
  it only when a device suggests a rate.

(services-settings-values)=
### Settings values

- **`KnobMode`** is how dragging a knob changes it: `KNOB_MODE_LINEAR` (0),
  `KNOB_MODE_SCALED_LINEAR` (1), `KNOB_MODE_ROTARY_ABSOLUTE` (2) and
  `KNOB_MODE_ROTARY_RELATIVE` (3), Rack's values, also exported from the
  module. `knob_mode` is one.
- **`BrowserSort`** is the module browser's order: `BROWSER_SORT_UPDATED`
  (0), `BROWSER_SORT_LAST_USED` (1), `BROWSER_SORT_MOST_USED` (2),
  `BROWSER_SORT_BRAND` (3), `BROWSER_SORT_NAME` (4) and
  `BROWSER_SORT_RANDOM` (5). `browser_sort` is one.
- **`ModuleInfo`** is Rack's record of one module in the browser, a value:
  `enabled` (true at first), `favorite`, `added` (how many times it was
  added) and `last_added`, a Unix time, or NaN if never. `module_infos`
  maps plugin slugs to module slugs to copies; a `ModuleInfoView` from
  `get_module_info` has the same fields and writes Rack's.
- **`PluginWhitelist`** is Rack's record of which of one plugin's modules
  the user subscribed to, a value: `subscribed` (the whole plugin) and
  `module_slugs`, a set copy (assign a new set). `module_whitelist` maps
  plugin slugs to copies. `is_module_whitelisted(plugin, module)` is
  Rack's check: true for a plugin with no entry or a subscribed one, else
  whether `module_slugs` has the module.

(services-mutex)=
## Rack's shared mutex: `rackpy.raw.SharedMutex`

`SharedMutex` is Rack's read-write lock: many readers, or one writer. It
has Rack's `lock`, `try_lock`, `unlock`, `lock_shared`, `try_lock_shared`
and `unlock_shared`, and `with mutex:` holds it for writing.
`SharedLock(m)` is Rack's `SharedLock<TMutex>`: it holds `m` shared from
construction until `release()`, or until its `with` block ends.

(services-shared-lock)=
### `SharedLock` over any `TMutex`

Rack's `SharedLock<TMutex>` only calls `m.lock_shared()` when it is made and
`m.unlock_shared()` when it ends, so `m` can be any object with those two
methods, not only a `SharedMutex`:

- **A `SharedMutex`** (exactly that class) is held natively, with the
  misuse checks and the GIL released while it waits, below.
- **Any other object**, a Python subclass of `SharedMutex` included, has its
  `lock_shared()` and `unlock_shared()` called from Python, with the GIL
  held and no native lock held, so a Python lock that waits (and lets the
  GIL go) cannot deadlock with it. An object without both methods raises
  `TypeError`.
- **Errors propagate.** If `lock_shared()` raises, no lock is made. If
  `unlock_shared()` raises in `release()` or at the end of a `with` block,
  the error propagates and the lock still counts as held, so `release()`
  can try again; a successful `release()` happens once.
- **Dropped while held,** a lock calls `unlock_shared()`, as C++ would at the
  end of its scope, but only on the thread that made it; an error there is
  reported through `sys.unraisablehook`. On another thread, or while Python
  shuts down, it calls no Python: `unlock_shared()` is not called, the drop
  is reported as unraisable (at shutdown, in Rack's log), and the object
  stays as it was. Release a lock explicitly to let go of it reliably.

Rack's mutex asserts on every pthread error, which would stop Rack. RackPy
records which threads hold the mutex and raises `ValueError` before pthread
sees a misuse:

- a thread locking a mutex it already holds;
- a thread unlocking a mutex it does not hold.

Other risks remain:

- **Waiting releases the GIL,** so a Python thread holding the mutex can
  still run and let it go.
- **Deadlocks between threads are not detected.** Two threads that each
  wait for a mutex the other holds deadlock, as in C++.
- **A mutex dropped while held is leaked.** Destroying a held rwlock is
  undefined, so RackPy logs a warning and leaks it instead.
- **Not for Rack's audio path.** A blocking lock does not belong there, and
  Python never runs there.

(services-weakptr)=
## Weak pointers: `rackpy.raw.WeakPtr`

In the `rack.hpp` closure, Rack's `WeakBase` is a base of `Widget`. So a
`WeakPtr` points at a widget, RackPy's or Rack's own, and `get()` returns
`None` once Rack deletes it. Weak pointers work on the main thread only,
because Rack's are not thread-safe.

(services-weakptr-contract)=
### The widget contract

This is the contract for `WeakPtr<T>`, `WeakBase` and `WeakHandle`
(owner decision, 2026-10-27):

- **`T` is any widget.** `WeakPtr<T>` needs `T` to derive from `WeakBase`
  (Rack's `set()` uses `T::weakHandle`, `weakptr.hpp:64-88`), and in Rack
  2.6.6 `Widget` is the only `WeakBase` subclass (`widget/Widget.hpp:21`).
  So the instantiations are `WeakPtr<Widget>` and its subclasses, as Rack's
  own `WeakPtr<ModuleWidget>`, `WeakPtr<PortWidget>` and
  `WeakPtr<TextField>` are. One `WeakPtr` class covers them: `get()`
  returns the widget's own Python object or host view, so no subclass's type
  is lost. Anything that is not a widget (or `None`) is refused.
- **`WeakBase` is `Widget`'s base.** Its `weakHandle` field and
  `getWeakCount()` are `Widget.weak_handle` and `Widget.get_weak_count()`, and
  `~WeakBase` runs when a widget is deleted, which clears its handle's
  pointer (`get()` turns `None`). A plugin's own non-widget C++ class
  deriving from `WeakBase` is that plugin's code, not Rack's API; Python
  objects have `weakref` for that.
- **`WeakHandle(void*)` runs inside `WeakPtr`.** Rack's `WeakPtr::set()` makes
  an object's handle with `new WeakHandle(ptr)` when it has none
  (`weakptr.hpp:80-83`) and deletes it with its last pointer
  (`weakptr.hpp:66-77`), as `WeakPtr(widget)` and `set(widget)` do here. A
  handle made any other way belongs to no object and no pointer, so only
  Rack makes usable ones, as with the classes only Rack can make
  ([coverage policy 11](../coverage.md#completeness-policy)); `WeakHandle`
  is a view, and Python cannot construct one.

- **Rack's members.** `WeakPtr()`, `WeakPtr(widget)`, `get()`, `set()`,
  truth testing, and `__copy__` (Rack's copy). `weak_handle` is the
  `WeakHandle` the pointers share.
- **On widgets.** `Widget` and `HostWidget` have Rack's
  `get_weak_count()` and `weak_handle`.
- **Handles.** A `WeakHandle` view has `ptr` (the widget, or `None`) and
  `count`. The view holds the handle alive, so it counts as one of its
  pointers.

## The facade: `rackpy.services`

- **`rackpy.services.system`.** `read_bytes`, `write_bytes`, `entries`
  (`pathlib.Path`s), `temp_directory`, `archive` (to a path, or as bytes),
  `unarchive(source, directory, trust=False)` and `open_url`. Unless trusted,
  `unarchive` reads the archive with Python's `tarfile` first and raises
  `ValueError` for an absolute or `../` entry, or a link out of the
  directory, before Rack writes anything.
- **`rackpy.services.network`.** `get_json(url, params)`, `post_json`,
  `put_json` and `delete_json`, plus `download(url, filename, on_progress)`,
  which runs Rack's download on a worker thread and calls `on_progress`
  with the fraction on the calling thread. JSON helpers return `rackpy.types.JSON`;
  `None` may be either a JSON null or a failure. Narrow the result before
  treating it as an object or an array. Also `encode_url` and `url_path`.
