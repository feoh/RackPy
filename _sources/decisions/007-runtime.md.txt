# ADR 007: The embedded runtime

- **Status:** Accepted for Linux x86_64, Rack Free 2.6.6.
- **Date:** 2026-10-22
- **Task:** RP-10
- **Evidence:** [tests/runtime](../../tests/runtime/test_runtime_host.py),
  [native/tests/test_runtime.cpp](../../native/tests/test_runtime.cpp),
  [tests/unit](../../tests/unit/test_errors.py)

## Context

ADR 003 set the rules for threads, lifetimes and teardown, measured with spike
code. RP-10 implements them as the runtime that every binding will use, before
any Rack API is bound. This records how, and the choices ADR 003 left open.

## Decisions

1. **Layout.** `native/runtime/*.cpp` with headers under
   `native/include/rackpy/runtime/`. A header named `context.hpp` on the include
   path shadows Rack's own (Rack includes it with angle brackets), so runtime
   headers are always included as `rackpy/runtime/...`. The Python-free parts
   (threads, diagnostics, context binding) compile without pybind11, so audio
   code and native tests can use them.
2. **Thread roles without hooks into Rack.** A thread with a Rack context that
   is not the main thread is one of Rack's own (engine worker, audio or MIDI
   driver): ADR 003 found that they set their context themselves and that
   threads Python starts have none. Python may run on the main thread and on
   context-less threads only.
3. **One dispatcher.** Every Rack-to-Python call goes through `callPython`:
   it refuses Rack's threads (counting and recording the refusal without
   allocating), takes the GIL, catches every Python and C++ exception, logs it,
   and drains deferred releases. Nothing propagates into Rack.
4. **Context binding and generations.** The first Rack context a RackPy module
   or module widget constructor sees is bound (`bindCurrentContext()`), and a
   sentinel widget is added to its scene. Any other context is refused, with one
   log line and a `foreign_context` diagnostic. Context and object tokens carry
   generation numbers that are never reused, so an object at a reused address
   never matches an old handle.
5. **Handles.** `Borrowed<T>` holds the raw pointer and a shared `ObjectToken`;
   deletion only clears an atomic. Python gets one wrapper per (address,
   generation) through an identity map of weak references, cleaned up when the
   wrapper is collected.
6. **Engine calls from Python** check the context, refuse inside
   exclusive-lock callbacks (`ExclusiveScope`, ADR 003 decision 3), release the
   GIL, then take an `EngineLease`: a shared hold on the context that also scopes
   it for the calling thread. Teardown takes the lease exclusively.
7. **References native objects hold** are `PyRef`s. Destroyed without the GIL
   (for example on an engine thread), a `PyRef` queues its object in a
   lock-free, fixed-size (4,096) deferred-release queue instead of touching
   Python; the sentinel's UI-thread `step()` and every dispatch drain it. If
   the queue is full the object leaks, with a diagnostic, rather than being
   released unsafely.
8. **Transferred objects** (pybind11 `classh` with
   `trampoline_self_life_support`) take the GIL in their destructor, so they
   must never be deleted after finalization. Bindings count each transfer and
   each deletion.
9. **Finalization is refused, not risked.** At plugin `destroy()` RackPy runs
   `rackpy._bootstrap.stop()`, every registered native detach hook
   (`onFinalize`), drains releases, clears identities and cached error types and
   collects garbage. If any `PyRef` or transferred object is still alive, it
   leaves the interpreter running and logs why. Otherwise it calls
   `Py_FinalizeEx`.
10. **Diagnostics** are a 256-slot ring of fixed-size records guarded by
    per-slot sequence locks, every field atomic. Any thread may record one
    without locking, allocating, logging or calling Python. Readers skip torn
    or overwritten records.
11. **Errors** are Python classes in `rackpy.errors`, each also deriving from
    the matching built-in. C++ throws `runtime::StaleHandle` and the others;
    one pybind11 translator maps them, caching the classes until finalization.
12. **Test hooks** (`-DRACKPY_TEST_HOOKS=ON`) add a `RuntimeProbe` module, a
    transferable `ProbeChild` widget and `_rackpy_host._testing`, so the tests
    can drive the runtime inside Rack. Production builds contain none of it,
    which `tests/native` and the smoke test check.

## Results

Rack Free 2.6.6, Linux x86_64, under Xvfb and headless (`uv run pytest
tests/runtime`, 40 tests, about 18 s on the planning machine):

- 1,000 module create/delete cycles: all 1,000 handles stale afterwards;
  identity entries 1 → 1; native references 1 → 1; Python objects 11,445 →
  11,444; RSS +0 KiB.
- Stale, cross-context, wrong-thread, double-transfer and engine-reentry cases
  raise the documented errors; Python on an engine thread is refused and
  recorded; an object dropped on an engine thread is released on the UI thread.
- Shutdown order observed: closing hooks (a failing one logged, the others
  still run), worker joined, context closed, a careless worker then sees
  `StaleHandleError`, Rack deletes the engine, `Py_FinalizeEx` returns 0.
- A deliberately leaked native reference makes RackPy refuse to finalize, and
  Rack still exits 0. Five Rack processes in a row load and unload cleanly.

## Consequences and limits

- Family tasks bind Rack types with `Borrowed<T>`, `identity()`, `PyRef`,
  `callPython`, `EngineLease` and the error types, and register an
  `onFinalize` hook for any callback registry.
- Rack never calls `destroy()` for a plugin it rejects after `init()`
  (manifest mismatch), so the interpreter is not finalized then.
- Multi-context hosts (Rack Pro in a DAW) remain unsupported: a second context
  is refused, not served.
- The ring keeps 256 diagnostics and truncates messages to 159 bytes.
- A transferred Python object used after Rack deleted it raises pybind11's own
  error rather than `StaleHandleError`; the widget bindings (RP-23) decide how
  to present that.
