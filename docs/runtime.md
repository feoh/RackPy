# The embedded runtime

How RackPy runs Python inside Rack: when the interpreter starts and stops,
which threads may run Python, how Python refers to things Rack owns, what goes
wrong and how you find out. The rules come from measurements in a real Rack
([ADR 002](decisions/002-embedding-and-bindings.md),
[ADR 003](decisions/003-lifetimes-and-threads.md)); the implementation is
recorded in [ADR 007](decisions/007-runtime.md).

:::{admonition} What exists today
:class: note

The runtime described here is implemented and tested in Rack Free
{{rack_version}} on Linux x86_64. The Rack API itself is not bound yet, so the
only Python modules you can use are {py:mod}`rackpy.errors` and
{py:mod}`rackpy.runtime`.
:::

## One interpreter, started by the plugin

A RackPy plugin ships its own CPython {{python_version}} in `python/`. When
Rack loads the plugin, the loader checks that runtime and starts it, on Rack's
main thread, before Rack has even created its window. RackPy never uses a
Python installed on the system.

The interpreter runs **isolated**: no `site` packages, no user site directory,
and no environment variables (`PYTHONPATH`, `PYTHONHOME` and the rest are
ignored). `sys.path` holds the plugin's `pythonpath/` directory and the bundled
standard library, nothing else; the current directory is not on it. No
bytecode is written into the installed plugin.

**Only one interpreter per Rack process.** If any other CPython is already
loaded, because another plugin embeds one, or one was preloaded into the
process, the RackPy plugin refuses to start, logs why, and registers no
modules. Rack keeps running without it. A second RackPy plugin is refused the
same way. This is a limit of CPython embedding, not a policy choice.

## Trusted code only

RackPy runs the Python files shipped inside the plugin, and nothing else.
Patch files never contain or run code. Python in a plugin has the same power
as the plugin's C++ would have: RackPy is not a sandbox, and a plugin you
install is code you trust.

## Threads

| Thread | May run Python? |
| --- | --- |
| Rack's main (UI) thread | Yes. Most of the Rack API is for this thread only. |
| Rack's engine and audio/MIDI driver threads | **Never.** If Rack calls into Python there, RackPy refuses the call and records a `refused_rack_thread` diagnostic. Audio stays native. |
| Threads you start | Yes, for work that is not tied to the UI. Calls that need the main thread raise {py:class}`~rackpy.errors.ThreadError`. |

Engine calls from your threads release Python's GIL while they wait for
Rack's engine lock, so a slow Python thread cannot stall Rack and Rack cannot
deadlock against Python.

Rack runs with the CPU's flush-to-zero and denormals-are-zero modes, and
threads you start inherit them, so subnormal floats read as zero everywhere in
the process, Python included ([values](reference/values.md#numbers-cross-as-c-numbers)).

## Handles to Rack objects

Rack owns its modules, widgets, cables and engine. Python holds **handles**
to them, never the objects. When Rack deletes an object, every handle to it
becomes **stale**: using it raises {py:class}`~rackpy.errors.StaleHandleError`
instead of touching freed memory. A new object, even at the same address, gets
a new handle.

Asking twice for the same Rack object gives you the same Python object
(`a is b`), for as long as you keep a reference to it.

Objects you create in Python and hand to Rack, such as a widget added to a
panel, belong to Rack after the transfer. Rack deletes them when it is done;
handing the same object over a second time raises
{py:class}`~rackpy.errors.OwnershipError`.

## When Rack shuts down

Rack gives plugins no "closing" event, so RackPy installs one. In order:

1. Your {py:func}`~rackpy.runtime.on_context_closing` callbacks run, newest
   first, on the main thread, while every handle still works. **Stop and join
   your threads here.** A callback that raises is logged and the rest still
   run.
2. RackPy waits for engine calls already in progress, then marks the Rack
   context closed. From now on every handle raises
   {py:class}`~rackpy.errors.StaleHandleError`.
3. Rack deletes its modules, widgets and engine.
4. When Rack unloads the plugin, RackPy releases every Python object that
   native code still held and finalizes the interpreter.

If a native object still holds a Python object at step 4, finalizing would
free memory something still uses, so RackPy **leaves the interpreter
running** instead and logs `RackPy did not finalize the interpreter`. Rack is
exiting at that point anyway.

## Errors

Every error RackPy raises derives from {py:class}`~rackpy.errors.RackPyError`
and from the built-in exception you would expect:

| Error | Also a | Raised when |
| --- | --- | --- |
| {py:class}`~rackpy.errors.StaleHandleError` | `ReferenceError` | the Rack object, or its Rack context, is gone |
| {py:class}`~rackpy.errors.ContextError` | `RuntimeError` | a handle is used under a different Rack context |
| {py:class}`~rackpy.errors.ThreadError` | `RuntimeError` | a main-thread operation is called from another thread |
| {py:class}`~rackpy.errors.OwnershipError` | `ValueError` | an object cannot be handed to Rack, for example twice |
| {py:class}`~rackpy.errors.EngineLockError` | `RuntimeError` | a call that takes the engine lock is made inside a callback that Rack runs with the lock already held, such as a module reset |

**Python errors never reach Rack.** When Rack calls your Python and it raises,
RackPy catches the exception at the boundary, writes it to Rack's log
(`log.txt` in your Rack user folder) and records a `callback_raised`
diagnostic. Rack carries on.

## Diagnostics

Some problems cannot be raised, because they happen where Python may not run,
or inside Rack's own calls. RackPy records those in a small ring of the most
recent 256 events, which {py:func}`rackpy.runtime.diagnostics` returns.
Recording one never blocks or allocates, so it is safe on the audio thread.

| Code | Meaning |
| --- | --- |
| `callback_raised` | a Python callback raised; see Rack's log for the traceback |
| `refused_rack_thread` | Rack tried to run Python on one of its own threads |
| `foreign_context` | a second Rack context asked for Python work and was refused |
| `release_queue_full` | too many Python objects were dropped on engine threads at once; one leaked |
| `shutdown_leak` | native objects still held Python objects at shutdown |

## When start-up fails

Start-up problems are reported in Rack's log, and Rack keeps running without
the plugin's modules:

| Log message | Cause and fix |
| --- | --- |
| `RackPy refused to start: bundled Python runtime missing or incomplete` | The plugin's `python/` directory is missing or for another Python version. Reinstall the plugin. |
| `RackPy refused to start: a CPython runtime is already loaded in this process` | Another plugin, or a preloaded library, already embeds Python. Remove one of them. |
| `RackPy host initialization failed (3): ...` | The plugin's Python raised while it was imported; the message ends with the exception. Fix the plugin code; RackPy has already shut the interpreter down cleanly. |
| `RackPy refused to start: ... rackpy-loader.json ...` | The plugin's loader configuration is damaged. Reinstall the plugin. |

Nothing persists between Rack runs, so restarting Rack after a fix is enough.
