# ADR 003: Thread affinity, lifetimes and callback semantics

- **Status:** Accepted for Linux x64 Rack Free 2.6.6 on the evidence below.
  Audio-driver-thread paths are known from source only (no audio device in the
  tests). Rack Pro multi-instance hosting is unsupported.
- **Date:** 2026-10-21
- **Task:** RP-06
- **Evidence:** [spikes/lifecycle](../../spikes/lifecycle/README.md),
  [tests/integration/test_lifecycle_probe.py](../../tests/integration/test_lifecycle_probe.py),
  [callback matrix](../reference/callback-matrix.md),
  [upstream/callback-rules.json](../../upstream/callback-rules.json)

## How the evidence was gathered

1. Read the pinned Rack source (`061ccf63`) for every place that constructs,
   deletes or calls back into modules and widgets, and every lock each engine
   method takes.
2. Ran a probe plugin in Rack Free 2.6.6 with a window (inside Xvfb), in
   screenshot mode and headless. It logs the thread role of every callback and
   measures whether audio processing continues while a callback runs.
3. Classified all 434 public virtual methods from the RP-05 inventory with
   reviewed rules. `bin/callback-matrix check` fails if any is unmatched.

`tests/integration/test_lifecycle_probe.py` passes 7 tests in about a minute.

## What Rack does

### The engine lock

The engine has one `SharedMutex` (`Engine::Internal::mutex`).

| Operation (`src/engine/Engine.cpp`) | Lock | Callbacks it dispatches |
| --- | --- | --- |
| `stepBlock` (each audio block) | **shared**, for the whole block | `process`/`processBypass` on engine threads; `onExpanderChange` when a neighbour changed |
| `addModule`, `removeModule` | **exclusive** | `onAdd` + `onSampleRateChange`; `onRemove` and neighbours' `onExpanderChange` |
| `resetModule`, `randomizeModule`, `bypassModule` | **exclusive** | `onReset`, `onRandomize`, `onBypass`/`onUnBypass` |
| `addCable`, `removeCable` | **exclusive** | `onPortChange` |
| `setSampleRate` (via `setSuggestedSampleRate`), `setMasterModule` | **exclusive** | `onSampleRateChange`; `onSetMaster`/`onUnsetMaster` |
| `moduleFromJson`, `fromJson` (patch load) | **exclusive** | `fromJson` → `paramsFromJson`, `dataFromJson` |
| `toJson`, `moduleToJson`, `prepareSave` | **shared** | `toJson` → `paramsToJson`, `dataToJson`; `onSave` |
| `getNumModules` | **none** (unsynchronized read) | — |

Measured: while `onReset` ran (exclusive), `process()` advanced **0** samples in
30 ms. While `dataToJson` ran (shared), it advanced **800**. Saving therefore
runs concurrently with audio; edits stop it.

### Threads

| Callback family | Thread observed or traced |
| --- | --- |
| Module construction, `onAdd`, `onRemove`, reset, randomize, bypass, cable changes, JSON, `onSave`, deletion | Main thread |
| `process`, `processBypass` | Engine threads only; four distinct threads with `threadCount` 4 |
| `onExpanderChange` | Engine thread during processing; main thread during removal |
| `onSampleRateChange`, `onSetMaster`, `onUnsetMaster` | Main thread without an audio device. Core's Audio module calls `setSuggestedSampleRate` and `setMasterModule` from `processInput`, so with a device they run on the **audio driver thread**, under the exclusive lock (source: `core/Audio.cpp:41-68`) |
| Audio port callbacks, MIDI `Input::onMessage` | Driver threads (`audio.cpp`, `midi.cpp`) |
| `Quantity` value and range methods | Main thread from widgets, **and** the engine thread from Core MIDI-Map's `process()` (`core/MIDIMap.cpp:101-131`) |
| Widget step, draw, input, context menu, GL context events | Main thread |

Other facts:

- `contextGet()` is **thread-local** (`context.cpp:48`). Engine workers and
  driver callbacks set it themselves. A thread Python creates has none: the
  probe's worker saw `has_context=False`.
- `ModuleWidget::~ModuleWidget` calls `setModule(NULL)`, which runs
  `engine->removeModule` (exclusive) and then `delete module`, both on the main
  thread.
- Browser previews and screenshots construct widgets with
  `createModuleWidget(NULL)` and step and draw them. The probe confirmed this.
- `ContextCreateEvent` and `ContextDestroyEvent` fire only when the window is
  constructed or destroyed (`window/Window.cpp:369, 378`). Rack 2.6.6 standalone
  never re-creates its window at runtime (that code is commented out in
  `adapters/standalone.cpp`). `onContextDestroy` reaches widgets at shutdown,
  before they are deleted.
- At shutdown `~Context` deletes, in order, the window, patch manager, scene
  (and with it every module widget and module), event state, history and
  **then** the engine. Plugin `destroy()` runs after all of that. Rack gives
  plugins no "context closing" callback.

## Decisions

### Thread rules

1. **No Python on engine or driver threads in production.** Every Rack-to-Python
   dispatch goes through one helper that refuses (and counts) calls from engine
   threads. Across every probe run, GIL acquisitions on engine threads were
   **0**.
2. **Every binding that can take the engine lock releases the GIL first.** The
   probe proved why. A Python thread that kept the GIL while waiting for the
   shared lock deadlocked against a Python `onReset` override that Rack ran
   under the exclusive lock. A watchdog killed it after 30 s. With the GIL
   released, the same worker made 17 million locking calls during 200 such
   callbacks with no deadlock. RP-11's binding manifest records
   `locks_engine` for each engine method from the lock table above, and the
   generator emits `gil_scoped_release` for them.
3. **Engine calls are refused inside exclusive-lock callbacks.** `SharedMutex`
   is not recursive, so a locking engine call from inside `onReset` would
   deadlock its own thread. RackPy marks the thread while it dispatches an
   `ui-exclusive` callback, and such calls raise `RuntimeError` instead. The
   probe confirmed the refusal.
4. **Engine calls from Python threads scope the bound context.** They set it
   for the duration of the call and restore the previous value.
5. **Python runs only inside the strategy its callback allows.** See the
   matrix:

| Strategy | Methods | What a Python override may do |
| --- | --- | --- |
| `ui` | 318 | Anything UI-side; the trampoline takes the GIL |
| `ui-exclusive` | 19 | Run briefly with audio stopped; no locking engine calls. RP-07 sets a time budget. |
| `ui-shared` | 5 | Return state synchronously. Audio is running, so read module state only through atomics or snapshots. |
| `engine` | 6 | Nothing in production. Python only in the RP-22 laboratory. |
| `driver` | 11 | Nothing. Python receives a queued notification on the UI thread instead. |
| `mixed` | 10 | Nothing. A native implementation serves both threads; Python configures it. |
| `lifecycle` | 33 | Destructors run on the deleting thread (main for widgets and modules) |
| `value` | 1 | Not a callback (`Exception::what`) |
| `unresolved` | 31 | Audio and MIDI `Driver`/`Device` implementations. **Blocks RP-29**, which must decide whether Python may implement drivers. |

### Ownership and lifetime

6. **Rack-owned objects are borrowed through tokens.** Modules, widgets in
   Rack's tree, cables, the engine and the context are never owned by Python.
   A handle holds a shared token. Deletion flips an atomic flag and does no
   Python work. Later use raises `ReferenceError`. In the stress run, all 40
   deleted modules' handles raised it from a Python thread, alongside 1.9
   million valid reads.
7. **Python-created objects that Rack will own are transferred.** Widgets and
   menu items use pybind11 `py::classh` with `trampoline_self_life_support`
   (ADR 002). Rack frees them with `delete` on the main thread; the trampoline
   then releases the Python object under the GIL.
8. **Python-owned values are copies.** `Vec`, `Rect`, colours and snapshots
   own their storage; buffers keep it alive.
9. **Identity.** One Python wrapper per live Rack object, keyed by
   (pointer, token generation), never by address alone, because addresses are
   reused. Implementation: RP-10.
10. **Scoped views.** Wrappers for `DrawArgs`, events, the menu passed to
    `appendContextMenu` and the JSON passed to `dataFromJson` are valid only
    during the callback and raise afterwards. Implementation: RP-10, RP-25.
11. **Strong references.** C++ keeps registered Python callables alive with
    strong references and drops them before finalization. *Every* `py::object`
    must be gone before `Py_FinalizeEx`: in the probe, a local `py::dict` that
    outlived it crashed Rack at exit (SIGSEGV in `_Py_Dealloc`).

### Context and teardown

12. **One bound context.** RackPy binds the first `Context` it sees, which the
    first module constructor provides (plugin `init()` runs before the context
    exists). Python work for any other context is refused until RP-10 adds
    context-generation tokens. Multi-instance hosts are unsupported until then.
13. **Teardown ordering.** All of these steps are tested:
    1. RackPy attaches a sentinel widget to the scene. Its `onContextDestroy`
       (windowed) or its destructor during scene deletion (headless) is the
       earliest teardown signal, and comes before the engine is deleted.
    2. There, RackPy calls Python to stop and join its worker threads.
    3. It then takes its own lease exclusively, so in-flight engine calls from
       Python finish, and marks the context dead. Later calls raise
       `ReferenceError`.
    4. Rack deletes modules and the engine. Module deletion flips tokens only.
    5. Plugin `destroy()` invalidates handles, lets Python clean up, drops every
       strong reference and calls `Py_FinalizeEx` (which returns 0).
14. **No window re-creation.** Rack 2.6.6 standalone does not re-create its
    window, so GL resources (fonts, images, framebuffers) are released on
    `onContextDestroy` and never rebuilt. A host that does re-create the window
    would send `onContextCreate` again; RackPy will rebuild caches lazily, but
    that path is untested.

### State and queues (designed, measured in RP-07)

15. **Synchronous JSON adapters.** `dataToJson` calls Python on the UI thread
    and converts its result before returning, as Rack requires; the probe did
    this. `dataFromJson` hands Python the JSON while audio is stopped. Neither
    defers work; a deferred answer is not equivalent.
16. **Bounded queues; the audio thread never waits.** UI-to-engine changes go
    through a fixed-capacity single-producer, single-consumer ring that
    `process()` drains without locking; overflow coalesces or drops and is
    counted. Engine-to-UI notifications (expander changes, sample-rate changes,
    driver events) go into a ring or a double-buffered snapshot that a
    UI-thread step drains into Python. Only atomics were exercised here; RP-07
    measures the queue design, and RP-14, RP-17 and RP-21 implement it.

## Consequences and blocked work

- **RP-10** implements the bound context, generation tokens, identity registry,
  scoped views, the sentinel and the lease.
- **RP-11** adds `locks_engine` and the callback strategy to every manifest
  entry, and fails generation for an engine-locking binding without
  `gil_scoped_release`.
- **RP-14** must keep `ui-exclusive` Python short and refuse engine re-entry.
  **RP-13/RP-14** implement `Quantity` value and range methods natively.
- **RP-22** owns every `engine` callback's Python laboratory path.
- **RP-29** is blocked on the 31 unresolved driver and device methods.
- **RP-07** must set the `ui-exclusive` time budget and measure the queues.

## Not covered

- Audio and MIDI driver threads were not exercised: the tests use no audio
  device, to avoid touching the machine's sound system. Their rows come from
  source.
- Rack Pro, DAW hosting and multiple contexts are untested and unsupported.
- macOS and Windows are untested.
