# Security and trust

RackPy runs Python inside Rack. This page says what that Python may do, what
RackPy refuses to run, how that is tested, and what it does not protect
against. To report a vulnerability, see
[SECURITY.md](../../SECURITY.md).

:::{admonition} RackPy is not a sandbox
:class: warning

A RackPy plugin's Python is trusted code, exactly as a C++ plugin is. It runs
with your user account's rights and can read and write files, use the
network, start programs and change anything in Rack. Nothing in RackPy makes
untrusted Python safe, and nothing should be read as a promise that it does.
:::

## What trusted code can do

| Capability | How |
| --- | --- |
| Files | Python's `open`, `pathlib` and `os`; Rack's [system services](../reference/services.md) |
| Network | Python's `socket` and `urllib`; Rack's requests ({py:mod}`rackpy.services.network`) |
| Programs | Python's `subprocess` |
| Rack | Every bound API: the engine, modules, cables, the patch history, settings, the window |
| Threads | Python threads; calls that need Rack's main thread refuse others ({py:class}`~rackpy.errors.ThreadError`) |
| Audio | Only through native kernels: Python never runs on Rack's engine or driver threads |

That is the same reach as a C++ plugin, so the same rule applies: install
only plugins you trust.

## What RackPy never runs

RackPy runs the Python files shipped in the plugin's `pythonpath/` folder,
its entry module first, and the standard library of the Python runtime the
plugin bundles. Nothing else, whatever the computer, the environment or the
patch says. Each rule below has a test in actual Rack that plants a file
which would leave a marker if Python ever ran it
(`tests/hardening/test_security.py`).

| Never runs | Why |
| --- | --- |
| Anything a patch carries | A module's saved data reaches `from_json` as plain JSON (dicts, lists, strings, numbers). RackPy never evaluates, unpickles or imports anything from a patch, its extra keys or its patch storage folder, and patch storage is never on Python's path. |
| Python environment variables | The interpreter starts isolated: `PYTHONPATH`, `PYTHONHOME`, `PYTHONSTARTUP`, `PYTHONUSERBASE`, `PYTHONWARNINGS` and the rest are ignored, and so are the working directory and user site-packages. |
| `.pth`, `sitecustomize` and `usercustomize` files | Python's `site` module is not imported, in the plugin's folder or anywhere else. |
| Plugin files named like the standard library | The plugin's `pythonpath/` comes after the standard library, so a stray `json.py` or `random/` never replaces the standard one for RackPy or for other code; Rack's log names it. |
| Files outside the plugin | The loader refuses a host library or `pythonpath` that resolves (through symlinks, too) outside the plugin folder, and C++ kernel libraries must also resolve inside it. |
| Another Python runtime | If another CPython is already in Rack's process (another plugin embeds one, or one was preloaded), RackPy refuses to start and says so. A second RackPy plugin is refused the same way. |

No bytecode is written into the installed plugin.

The loader's own configuration, `rackpy-loader.json`, is checked before
anything loads: unreadable JSON, a wrong ABI, absolute paths, `..`,
backslashes, or an entry that is not a module name are each refused with a
message in Rack's log, and Rack goes on without the plugin.

## Failures stay contained

A Python error never crosses into Rack. Every place Rack calls Python catches
it, counts it, records it in {py:func}`rackpy.runtime.diagnostics` and logs
it; Rack gets a defined answer instead (its own implementation, no data, a
placeholder module). A module's audio does not depend on its Python: its
native kernel keeps running when a Python callback fails, and a kernel that
fails itself is stopped, with the module's outputs set to silence. Rack's log
gets the first 20 errors, then at most one a second, so a callback that fails
on every frame cannot fill the disk; the diagnostics record keeps the latest
256 and the counters count all of them.

Malformed values from Python are refused at the boundary with a documented
exception (`TypeError`, `ValueError`, `IndexError`, `OverflowError`, or a
{py:class}`~rackpy.errors.RackPyError`): seeded fuzzing of graph plans, JSON,
indices and counts, override return values and dead handles checks that
nothing else gets through (`tests/hardening/in_rack_fuzz`). A handle to an
object Rack has deleted raises {py:class}`~rackpy.errors.StaleHandleError`
instead of touching freed memory.

## Trusted code can still hurt the audio

Python never runs on Rack's audio path, so a slow or failing Python callback
cannot stop Rack's engine, with one exception that comes from Rack itself.
Rack calls a module's `fromJson`, reset, randomize and bypass events with its
engine lock held, and audio waits until they return. RackPy runs your handlers
for those there, as Rack runs C++ ones, so a slow `on_reset` is a dropout. Keep
them short: RackPy records `slow_exclusive_callback` for each that takes over
1 ms ([ADR 004](../decisions/004-performance-budgets.md)).

`tests/hardening/in_rack_stress` measures this in Rack, against the engine's
idle rate in the same run: callbacks sleeping 20 ms each outside that lock left
the engine's rate unchanged, callbacks raising on every call left it unchanged,
and every slow callback inside the lock was diagnosed. With the audio guard
preloaded, RackPy's own `process()` made no heap, lock, write or GIL call
throughout.

## Memory safety evidence and its limits

| Evidence | Covers | Does not cover |
| --- | --- | --- |
| AddressSanitizer and UBSan (`-m sanitizer`) | The native tests, and the sanitized plugin running live-Rack suites (the fuzzers and the 1,000-cycle stress included) inside actual Rack, with a deliberate overflow proving the sanitizer is active | Rack and CPython themselves, which are not instrumented |
| ThreadSanitizer | The native tests, including RackPy's lock-free queues and expander exchange | Rack's process: neither Rack nor CPython is built with it, so it cannot run there |
| 1,000 create/delete and save/reload cycles | RackPy's live references, transferred objects, deferred releases and identity entries return exactly; resident memory does not grow | Leaks inside Rack or CPython that RackPy's counters cannot see |

Linux x86_64 only: macOS and Windows have not been built or tested.

## Distributing a plugin that bundles Python

A RackPy plugin ships a CPython runtime, its standard library and extension
modules, and RackPy's own libraries. That is more native code than most
plugins, and it changes what a distributor must check.

- **Integrity.** A `.vcvplugin` is an archive; nothing checks that its files
  are the ones the author built. Anyone who can change them can make Rack run
  anything. Distribute through channels you trust, and publish checksums.
- **Signing.** On macOS, Rack's own signature allows unsigned libraries, but
  Gatekeeper's quarantine applies to downloaded archives, and each bundled
  library needs a consistent signature (RP-40). On Windows, unsigned DLLs
  may be blocked or flagged (RP-41). Neither platform is built yet.
- **Updates.** The bundled runtime receives CPython's security fixes only when
  the plugin is rebuilt and reinstalled. Rebuild when the pinned CPython
  publishes a security release.
- **Dependencies.** Anything vendored into `pythonpath/` is trusted code with
  the plugin's full reach. Pin and review it like a C++ dependency.

## Known gaps

- A Python-made MIDI or audio port dropped on a thread other than Rack's main
  thread is leaked, with a warning, rather than destroyed there.
- ThreadSanitizer cannot run inside Rack (see above), and the sanitizers do
  not cover Rack or CPython.
- Rack's download cannot be cancelled, so a failing progress callback in
  {py:func}`rackpy.services.network.download` waits for the download to end
  before raising.
