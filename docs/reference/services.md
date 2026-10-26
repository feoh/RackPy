# Services

Rack's services: text, random numbers and the log (**RP-30A**), with the
system, network, settings and synchronization services to follow
(RP-30B, RP-30C). **Implemented so far, Linux x86_64 inside Rack Free
2.6.6:** `rackpy.raw.string`, `rackpy.raw.random`, `rackpy.raw.logger` and
the logging macros (see the [coverage report](coverage.md)).

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
64-bit number, and `state` is its two state words. `rotl(x, k)` needs `k`
from 1 to 63: Rack's shift by 64 for `k` 0 is undefined in C++.

`local()` is Rack's own generator: one for the whole process, kept until
Rack quits. Rack's engine threads and UI use it without a lock, so
Python's draws race with theirs, as Rack's own do. `init()` reseeds it, and
`u64`, `u32`, `uniform` (`[0, 1)`), `normal`, `get(type)`, `get_uint32`
and the other `get_*`, `buffer(out)` (it fills a writable byte buffer) and
`vector(len)` draw from it. For reproducible numbers, make a generator of
your own.

(services-logger)=
## The log: `rackpy.raw.logger`

`log(level, filename, line, func, format, *args)` is Rack's log. It formats
the message as `string.f` does, then passes it to Rack's log as one `%s`
argument: the same text, without Python's format string ever reaching
Rack's printf. It is thread-safe and releases the GIL. `Level` has Rack's
levels. `get_log_path()` returns the file Rack logs to, and
`set_log_path()` sets Rack's variable, though Rack opened its log at
start-up, so later messages still go there.

`rackpy.raw.macros.DEBUG`, `INFO`, `WARN` and `FATAL` are Rack's macros.
Each logs at its level with the Python caller's file, line and function, as
the macros do with `__FILE__`, `__LINE__` and `__FUNCTION__`. As in Rack,
`FATAL` does not stop anything.
