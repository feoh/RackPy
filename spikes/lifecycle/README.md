# RP-06 lifecycle probe

**Spike code, not RackPy.** It records which thread runs each Module and
ModuleWidget callback in a real Rack host, and exercises the GIL, engine-lock,
context and teardown rules in
[ADR 003](../../docs/decisions/003-lifetimes-and-threads.md).

It reuses the RP-04 loader (`../embedding/src/loader.cpp`) unchanged and adds
one pybind11 host, `src/lifecycle.cpp`:

- `ProbeModule` overrides every Module event, process, bypass and JSON
  callback and logs `event=… role=main|engine|other tid=…`. `onReset` and the
  first `dataToJson` sleep 30 ms natively and report how many samples
  `process()` ran meanwhile, which shows the engine lock state.
- `ProbeWidget` logs construction (including the NULL-module browser preview),
  step, draw, context menu and GL context events. On the UI thread, the first
  widget drives a scripted scenario: resets that call Python under the
  exclusive lock, randomize, bypass, a sample-rate change, save and load, a
  context menu, module churn, then `Window::close()`.
- A `Sentinel` widget on the scene gives the earliest teardown signal. It
  joins Python workers and drains a RackPy-owned lease before Rack deletes the
  engine.
- `_rackpy_lifecycle.engine_module_count(release_gil=…)` takes the engine's
  shared lock from any thread. It scopes the bound Rack context, and refuses
  calls inside exclusive-lock callbacks.

`lifecycle_plugin.py` is the trusted script; `scenario.json` (written by the
test into the plugin directory) selects the worker mode.

```bash
uv run --with pytest pytest tests/integration/test_lifecycle_probe.py -v
```

Needs `xvfb-run` (package `xorg-server-xvfb`): Rack runs with a real window on
a virtual X display, using Mesa's software renderer, so nothing appears on the
desktop. One test deliberately deadlocks Rack and relies on a watchdog that
kills the whole process group.
