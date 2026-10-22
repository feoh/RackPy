# ADR 002: Embedding CPython and choosing a binding library

- **Status:** Accepted for Linux x86_64 on the evidence below. macOS and Windows
  have concrete risk probes only; nothing has run there.
- **Date:** 2026-10-21
- **Task:** RP-04
- **Evidence:** [spikes/embedding](../../spikes/embedding/README.md),
  [tests/integration/test_embedding_smoke.py](../../tests/integration/test_embedding_smoke.py)

## Context

RackPy runs inside Rack. It has to load a Python runtime from a Rack plugin, own
that interpreter safely, and expose Rack objects whose lifetimes Rack controls.
The pinned Rack source (`src/plugin.cpp` at `061ccf63`) and the spike
established these facts about the host:

- Rack opens each plugin library with `dlopen(RTLD_NOW | RTLD_LOCAL)`, and
  calls `dlclose` after `destroy()` at shutdown.
- `/tmp/Rack2` (the plugin `RUNPATH` that finds libRack) exists only while
  Rack's own `dlopen` runs. Rack removes it again before calling `init()`.
- A plugin whose manifest names a module that `init()` did not register is
  rejected, and Rack never calls its `destroy()`.
- Standalone Rack calls `init()` before it creates the `Context`
  (`contextGet()` is NULL). Modules and module widgets are created on the main
  thread with a context. At shutdown Rack deletes them while tearing down the
  scene, then the engine, and only then calls plugin `destroy()`.
- A patch that references an unavailable module makes Rack show a blocking
  `osdialog` message, even in headless mode.

## What was measured

The spike ran in **Rack Free 2.6.6, Linux x64, headless** (`Rack -h -u <dir>`)
with python-build-standalone `20261021` (CPython 3.14.9 and 3.15.1,
`install_only`). The test runs these scenarios, all passing (`10 passed`, about
50 s on the planning machine):

| Scenario | Result |
| --- | --- |
| Loader + pybind11 host, Python 3.14 and 3.15 | Isolated interpreter, stdlib imports, value type, borrowed handles, buffer, widget callback, native `process()` (over 1,000 samples per module), `ReferenceError` after deletion, `Py_FinalizeEx` returns 0, Rack exits 0 |
| Loader + nanobind host, Python 3.14 and 3.15 | The same, except that ownership transfer of a Python `Widget` subclass is refused (below) |
| `libpython` linked directly into `plugin.so` | Loads, but `import _dbm` fails with `undefined symbol: PyExc_ValueError`: `RTLD_LOCAL` hides `Py*` symbols from extension modules that do not link `libpython` |
| Loader with `libpython_global=false` (mutation check) | The same `_dbm` failure, so the test detects it |
| Runtime directory missing | Logged "bundled Python runtime missing or incomplete …", no interpreter, Rack exits 0 |
| 3.15 runtime where 3.14 is expected | Refused with the same message |
| Two RackPy plugins in one process | The first initializes; the second logs "a CPython runtime is already loaded in this process" |
| System `libpython3.14.so` preloaded (`LD_PRELOAD`) | Refused, naming the preloaded library |
| Trusted script raises during init | "host init failed (3): … deliberate spike failure", torn down, Rack exits 0 |

Binary facts for the Linux x64 build (GCC 16.2.1, SDK flags, `-std=c++17`):

| Artifact | Stripped size | Build time | Dynamic dependencies |
| --- | --- | --- | --- |
| Loader `plugin.so` | 80,472 B | 0.5 s | `libRack.so`, libc, libm |
| pybind11 host | 406,120 B | 6.5 s | `libRack.so`, `libpython3.14.so.1.0`, libc, libm |
| nanobind host | 353,840 B | 5.3 s (3.6 s of it libnanobind) | the same |

Both hosts need `GLIBC_2.35` because the host toolchain built them. The
python-build-standalone `libpython` needs only `GLIBC_2.17`, the same floor as
libRack ([compatibility](../compatibility.md#findings-that-constrain-later-tasks)).

### The deciding difference: who frees a Python subclass

Rack frees widget children, menu items and modules with plain `delete`. A
Python subclass handed to Rack therefore needs C++ heap storage that lives as
long as C++ wants, and its Python state has to survive until then.

- **pybind11 3.1.0** (`py::classh` with `trampoline_self_life_support`): the
  C++ object is separate from the Python object (`storage inside python
  object=False`). `attach()` takes a `std::unique_ptr<Widget>` and gives it to
  Rack's tree. `on_add` ran through the trampoline. At shutdown Rack's
  `delete` ran `on_remove`, then released the Python object (its `__del__`
  ran) on the main thread, before `Py_FinalizeEx`. No crash.
- **nanobind 3.1.0**: the C++ object lives inside the Python object (`storage
  inside python object=True`), and nanobind refuses the transfer: "could not
  transfer ownership of a Python instance of type 'spike_plugin.Child' to C++".
  Its alternative, `std::unique_ptr<T, nb::deleter<T>>`, frees with
  `Py_DECREF` rather than `delete`, so it cannot be handed to Rack either.
  Supporting Rack ownership would need a C++-allocated proxy object at every
  API that takes ownership.

## Decisions

1. **Two-stage loading.** `plugin.so` is a thin loader with no Python
   dependency. It checks for another interpreter, verifies the bundled runtime,
   opens `libpython` with `RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE`, then opens
   the host library with `RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE`. The loader
   and host talk through a C ABI (`host_api.h`). Every DSO links libstdc++
   statically, so no C++ exception or RTTI crosses that boundary.
2. **Bundled, relocatable CPython.** Each artifact ships one
   python-build-standalone `install_only` runtime in `<plugin>/python`. The
   loader never uses a system Python.
3. **Python ABI.** Start with **CPython 3.14**, the full (not limited) ABI,
   with the GIL (not the free-threaded build). 3.15.1 passed the same tests and
   is the next candidate; adding it is a per-artifact rebuild. 3.14 goes first
   because it has been out a year longer. Its scheduled end of life is
   2030-10.
4. **Interpreter ownership.** RackPy owns the only interpreter in the process.
   The loader refuses to start when any `libpython` is already mapped
   (`dl_iterate_phdr` also sees libraries other plugins opened `RTLD_LOCAL`) or
   `Py_IsInitialized()` is true. A refused plugin registers no models.
   Subinterpreters are not used.
5. **Isolated configuration.** `PyPreConfig_InitIsolatedConfig` with UTF-8 mode
   and `PyConfig_InitIsolatedConfig` with: `home` set to the bundled runtime,
   no `site`, no user site, no bytecode writing, no Python signal handlers, no
   C stdio changes and no `argv` parsing. `sys.path` is the plugin directory
   plus the bundled standard library. Errors are reported, never passed to
   `Py_ExitStatusException`, which would exit Rack.
6. **Thread and lifetime rules shown by the spike** (RP-06 extends them):
   initialize in `init()` on the main thread, then release the GIL;
   callbacks take it with `PyGILState_Ensure`; finalize in `destroy()`, after
   Rack has deleted every module and widget. If host init fails, the loader
   finalizes immediately because Rack will not call `destroy()`.
7. **Binding library: pybind11 3.1.0.** Every Rack type that Python may
   subclass and Rack may free uses `py::classh` and a trampoline derived from
   `trampoline_self_life_support`. nanobind is not used: it was smaller and
   faster to build here, but it cannot give Rack ownership of Python
   subclasses. The spike is too small to say how compile time scales to the
   whole API; RP-07 and RP-08 measure that and may split translation units.
8. **Borrowed Rack objects.** Python holds a shared token, never the Rack
   pointer. Deleting a module only clears an atomic flag. Later access raises
   `ReferenceError`. The production `process()` path makes no Python calls.
9. **Code provenance.** The host imports only trusted scripts shipped inside
   the plugin directory. Patch files never run code.

## Supported and refused combinations

| Combination | Status |
| --- | --- |
| Rack Free 2.6.6 standalone, Linux x64, with or without a window | Supported. Headless tested; windowed **untested** |
| Two RackPy plugins in one process | The second is refused (tested) |
| Another CPython already in the process (preloaded, or another plugin's) | Refused (tested by preload and by a second RackPy copy) |
| A system Python, or a runtime for the wrong version | Refused (tested) |
| Several Rack contexts in one process (Rack Pro hosting several instances in a DAW) | **Untested and unsupported.** Standalone Rack Free creates one Context and a plugin cannot create another. Until RP-06 and RP-10 add context-generation tokens, RackPy must refuse Python work for any context other than the first one it sees. |
| Rack Pro standalone | **Untested.** The planning machine's Rack Pro libRack differs from Rack Free's |
| Duplicate load of the same plugin slug | Rack itself refuses it ("already loaded") |

## Linux loading rules

- Loader: SDK flags, `RUNPATH /tmp/Rack2`, exports `init` and `destroy` only.
- Host: built with `-fvisibility=hidden`; exports `rackpy_spike_host_init`,
  `rackpy_spike_host_destroy` and the module init function.
  `RUNPATH /tmp/Rack2:$ORIGIN/python/lib`. Its `NEEDED libRack.so` resolved to
  the libRack Rack had already loaded, even after `/tmp/Rack2` was gone.
- `libpython3.14.so.1.0` is found by explicit path, not by search.
- `RTLD_NODELETE` on `libpython` and the host keeps them mapped after Rack
  `dlclose`s the loader, so no static destructor runs after finalization.

## macOS and Windows: risks to probe natively

Evidence gathered on Linux from the pinned installers, not from running them:

- **macOS.** The Rack Free app is a universal `x86_64` + `arm64` binary. Its
  embedded entitlements include `com.apple.security.cs.disable-library-validation`,
  `allow-jit` and `allow-unsigned-executable-memory`, so library validation
  should not block a separately signed `libpython`. RP-40 must check:
  `-undefined dynamic_lookup` together with an `RTLD_GLOBAL` libpython for
  extension modules; `@loader_path` install names; quarantine attributes on
  extracted `.vcvplugin` contents; ad hoc versus Developer ID signing of every
  bundled `.dylib` and `.so`; and both architectures natively.
- **Windows.** `libRack.dll` and `Rack.exe` import `msvcrt.dll`, and Rack ships
  its own `libstdc++-6.dll`, `libgcc_s_seh-1.dll` and `libwinpthread-1.dll`.
  CPython's official Windows builds use MSVC with the UCRT, so the process would
  contain two C runtimes. Windows `LoadLibraryW` does not search the plugin
  directory for dependencies, which the two-stage loader handles by loading
  `python3XY.dll` by full path (`LoadLibraryExW` with
  `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`). RP-41 must prove that a MinGW-built host
  can use an MSVC-built `python3XY.dll` with no CRT objects (`FILE*`, `malloc`)
  crossing, or choose a different runtime build.

## Blockers and alternatives

There is no blocker for Linux x64. If Windows cannot mix runtimes safely, the
alternatives are a MinGW-built CPython (as MSYS2 builds it) or an out-of-process
design. The out-of-process option would contradict the in-process owner
decision and needs owner review.

## Host-load trace

Final build, Python 3.14.9, pybind11 host, two modules; paths shortened. The
lines with `py:` come from the trusted script.

<details>
<summary>log.txt excerpt (43 lines)</summary>

```text
[0.000 info adapters/standalone.cpp:153 main] VCV Rack Free 2.6.6 Linux x64
[0.011 info src/plugin.cpp:133 loadPlugin] Loading plugin from <user>/plugins-lin-x64/RackPySpike
[0.013 info src/host_core.cpp:87 hostInit] RACKPY_SPIKE host init context=null
[0.024 info src/host_core.cpp:137 hostInit] RACKPY_SPIKE interpreter initialized binding=pybind11 3.1.0 python=3.14.9 (main, Oct 21 2026, 17:48:02) [Clang 23.1.
[0.024 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: python 3.14.9 prefix='<user>/plugins-lin-x64/RackPySpike/python' executable='<user>/plugins-li
[0.024 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: flags isolated=1 no_site=1 dont_write_bytecode=1
[0.083 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: import _dbm: ok
[0.083 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: value Vec(4, 6) x=4.0
[0.083 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: child finalized
[0.083 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: attach outside configure: RuntimeError: attach() is only valid inside Kernel.configure()
[0.083 info src/loader.cpp:127 loadHost] RACKPY_SPIKE_LOADER host host_pybind11.so ready (libpython global)
[0.083 info src/plugin.cpp:208 loadPlugin] Loaded plugin RackPySpike 2.0.0
[0.087 info src/patch.cpp:366 loadAutosave] Loading autosave <user>/autosave/patch.json
[0.087 info src/engine/Engine.cpp:1301 fromJson] Creating module RackPy Spike
[0.087 info src/host_core.cpp:38 SpikeModule] RACKPY_SPIKE module constructed thread=main
[0.087 info src/engine/Engine.cpp:1301 fromJson] Creating module RackPy Spike
[0.087 info src/host_core.cpp:38 SpikeModule] RACKPY_SPIKE module constructed thread=main
[0.087 info src/app/RackWidget.cpp:372 fromJson] Creating module widget RackPy Spike
[0.087 info src/host_core.cpp:69 SpikeWidget] RACKPY_SPIKE widget callback thread=main context=set
[0.087 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: configure id=11 alive=True snapshot format=f len=16
[0.087 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: child storage inside python object=False
[0.087 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: child on_add
[0.087 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: attach: ok
[0.087 info src/app/RackWidget.cpp:372 fromJson] Creating module widget RackPy Spike
[0.087 info src/host_core.cpp:69 SpikeWidget] RACKPY_SPIKE widget callback thread=main context=set
[0.087 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: configure id=12 alive=True snapshot format=f len=16
[0.087 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: child storage inside python object=False
[0.087 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: child on_add
[0.087 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: attach: ok
[1.993 info src/context.cpp:26 ~Context] Deleting scene
[1.994 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: child on_remove
[1.994 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: child finalized
[1.994 info src/host_core.cpp:44 ~SpikeModule] RACKPY_SPIKE module deleted id=11 processed=83790 thread=main
[1.994 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: child on_remove
[1.994 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: child finalized
[1.994 info src/host_core.cpp:44 ~SpikeModule] RACKPY_SPIKE module deleted id=12 processed=83790 thread=main
[2.004 info src/plugin.cpp:352 destroy] Destroying plugin RackPy spike
[2.004 info src/host_core.cpp:166 hostDestroy] RACKPY_SPIKE destroy thread=main
[2.004 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: stale id=11: ReferenceError (module 11 has been deleted)
[2.004 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: stale id=12: ReferenceError (module 12 has been deleted)
[2.004 info src/host_pybind11.cpp:63 operator()] RACKPY_SPIKE py: shutdown handles=2
[2.013 info src/host_core.cpp:171 hostDestroy] RACKPY_SPIKE Py_FinalizeEx rc=0
[2.013 info adapters/standalone.cpp:313 main] Destroying logger
```

</details>

## Consequences

- RP-06 builds on this lifetime model. It must test the GIL against Rack's
  engine lock when a widget is deleted while Python holds the GIL; every
  trampoline destructor acquires it.
- RP-08 scaffolds the loader/host split, the bundled runtime and pybind11 3.1.0
  pins. It also needs an old-glibc toolchain for distributable builds.
- RP-10 implements the context policy above.
- The runtime adds about 254 MB unpacked before trimming. RP-39 decides what
  ships.
- Windowed Rack, macOS and Windows remain untested. No support is claimed for
  them.
