# Proposed architecture

**Design proposal, not an implemented API.** Feasibility tasks may change
technology choices, but not the full-coverage goal without owner review.

## Two user experiences, one set of semantics

**Pythonic facade (`rackpy`).** Snake-case names, explicit enums, keyword
arguments, useful reprs, checked indexed/named port collections, properties for
cheap scalar access, pathlib at filesystem boundaries, and standard Python
exceptions. Expensive work remains a method, not a surprising property.

**Low-level mirror (`rackpy.raw`).** Preserve upstream namespaces, overload
meaning and callback capabilities with stable C++ → Python reference tables.
Do not require camelCase everywhere to achieve coverage. Aliases are for
discoverability and migration, not a second independently implemented engine.

An internal native extension and Rack-loadable C++ plugin host provide both.
RP-04 chose pybind11 3.1 over nanobind after measuring both in a real Rack host,
and chose a thin loader plugin that loads a bundled CPython before the binding host
([ADR 002](decisions/002-embedding-and-bindings.md)).
No ctypes-only or IPC-only approach can satisfy the in-process subclassing goal.

## Runtime sketch

```text
Rack process
  native RackPy plugin host
    process-wide CPython runtime owner
      plugin/module Python configuration and approved UI callbacks
      facade + raw binding modules
    per-Rack-context handles and ownership registry
    native DSP kernels and precompiled execution plans
    bounded command/snapshot channels between UI and engine
```

Do not assume a separately installed Python extension can resolve Rack symbols
outside Rack. An importable test harness and an in-host extension have different
link/load requirements; test both without confusing them.

## Python writes the instrument; native code performs it

Rack calls `Module::process()` **once per sample**, not once per audio block.
Production processing therefore runs native kernels with preallocated state.
Python constructs/configures kernels and a native execution plan outside the
audio thread. Rack ports, parameters and lights are connected to that plan
without per-sample crossings into Python.

The first planned graph supports enough native primitives for oscillator,
gain/VCA, polyphonic and control examples. It must also support documented
native extensions so arbitrary future DSP is not limited to a toy node library.
No implicit Python JIT or vectorization is assumed. An unrestricted Python DSP
language/compiler would be a separate project.

Graph compilation must preserve sample ordering, cable feedback, control
changes, and expander timing. Do not batch samples across Rack module boundaries
to make benchmarks look better.

## Honest callback coverage

A full binding must not hide `process`, `processBypass`, custom virtuals or
engine-thread callbacks. Plan a separately enabled **non-real-time callback
laboratory** for Python overrides and offline tests. It may expose per-sample
Python for semantic experimentation, with prominent warnings and no performance
guarantee. Prove this can be hosted using only public APIs before promising it.

Normal mode forbids Python execution on the audio thread. If offline operation
cannot provide a callback's contract, report that gap; a C++ implementation by
itself does not count as Python coverage. Do not queue a synchronous callback
and silently claim its semantics are unchanged.

Engine event bodies may be mutually exclusive with that module's process, but
this does not mean they run on the UI thread or cannot stall other audio work.
RP-06 must classify actual call sites and define native/snapshot adapters.
Serialization is synchronous: a deferred Python response is not interchangeable
with Rack's expected JSON return value.

## Lifetime and concurrency

- One supported CPython owner per process initially; no claim of subinterpreter
  safety. Detect other interpreters and incompatible runtimes.
- Configure isolated Python search paths. Do not inherit arbitrary current-directory
  imports or `PYTHONPATH` without an explicit trusted-user choice.
- Keep a runtime alive while any native callback or Python-owned state exists.
  Never call Python destructors from audio-thread deletion paths.
- Distinguish Rack-owned objects, Python-owned values, shared assets, and transferred
  children. Track identity and invalidate borrowed views when their owner dies.
- A context-bound handle is invalid outside its Rack instance. Restore thread-local
  context after scoped use. Never use a global APP pointer across unrelated instances.
- UI operations assert UI-thread affinity. Cross-thread changes use bounded,
  documented channels, not opportunistic direct reads of engine memory.
- Drawing args, event refs and message buffers have scoped lifetimes.
- A memoryview keeps valid backing storage alive or is rejected; “zero copy” is
  never an excuse for a dangling pointer.

## Exceptions and state

Validate Python indices before invoking Rack functions that only assert bounds.
Translate errors to stable exception categories. Catch every exception at C ABI
and virtual callback boundaries. Audio failures use deterministic silence or
bypass plus bounded native diagnostics read later; no audio-thread traceback.

Map Jansson to ordinary JSON-compatible Python values with explicit ownership,
range, nonfinite-float and depth policies. Preserve Rack's save/load ordering and
unknown-state behavior. Never serialize Python code or unpickle patches.

## Packaging proposal

Use a uv-managed Python project, CMake/scikit-build-core, and a Rack plugin
packaging adapter only after the spike proves their interaction with the SDK
toolchain. Pin CPython minor/ABI, binding library, compiler and SDK per artifact.
Publish a host plugin/runtime bundle and developer tooling as distinct artifacts;
a PyPI wheel alone is not a working Rack plugin.

Start platform validation with Linux x86_64; then independently prove macOS and
Windows architectures supported by the selected SDK. Do not claim an untested
architecture, Rack Pro, or a Python limited ABI.
