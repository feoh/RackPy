# RP-04 embedding spike

**Spike code, not RackPy.** It exists to prove, in a real Rack host, that a
Rack plugin can embed CPython and expose Rack objects through a binding
library. Its Python module, `_rackpy_spike`, is not a proposed API. The
results and the decisions they support are in
[ADR 002](../../docs/decisions/002-embedding-and-bindings.md).

## What is here

| File | Role |
| --- | --- |
| `src/loader.cpp` | `plugin.so`. Includes only `rack.hpp` plus `dlfcn.h`/`link.h`. Has no Python dependency. Refuses a second or foreign CPython, checks the bundled runtime, promotes `libpython` to `RTLD_GLOBAL`, then opens a host library. |
| `src/host_api.h` | C ABI between loader and host. Only C types cross it. |
| `src/host_core.{hpp,cpp}` | Rack module and widget, token-based borrowed handles, isolated `PyConfig` start-up and finalization. |
| `src/host_pybind11.cpp`, `src/host_nanobind.cpp` | The same Python API in each binding library: a value type (`Vec`), a borrowed Rack-owned object (`ModuleHandle`), a typed buffer, a Python-subclassable `Kernel` owned by C++, and a `Widget` subclass handed to Rack's widget tree. |
| `src/direct.cpp` | The naive design for comparison: `plugin.so` links `libpython` and the pybind11 host directly. |
| `spike_plugin.py` | Trusted script shipped in the plugin directory. Patch files never run code. |
| `inputs.json` | Pinned python-build-standalone runtimes and binding-library wheels. |

## Run it

```bash
uv run --with pytest pytest tests/integration/test_embedding_smoke.py -v
```

The test fetches and verifies every pinned input (`bin/rack-sdk path`,
`bin/rack-sdk host`, `bin/spike-inputs paths`), builds each variant for
Python 3.14 and 3.15, stages a temporary Rack user directory and runs Rack Free
2.6.6 headless with `-h -u <dir>`. It clears `DISPLAY` and `WAYLAND_DISPLAY`,
because Rack opens a blocking dialog, even headless, when a patch references an
unavailable module.

Linux x86_64 only. Do not run it while another Rack instance is starting:
Rack creates and removes `/tmp/Rack2` as it loads plugins.
