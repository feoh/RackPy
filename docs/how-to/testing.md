# Test a plugin

Most of a RackPy plugin can be tested without starting Rack: its processing is
a graph that runs in plain Python, and its event handlers run on a laboratory
bench. What needs Rack is checked by loading the plugin and reading Rack's log.

**Status:** Linux x86_64. The commands below were run as shown, from a
checkout with the quickstart's `MyPlugin` in it.

## Test the processing offline

A [graph](graphs.md) runs in pure Python with
{py:func}`rackpy.dsp.graph_reference.run`, to float32 precision, without Rack.
Write ordinary pytest tests against it. The oscillator tutorial's
`offline_reference` is such a function:

```{rackpy-example} testing_offline.py
:caption: Two tests of the tutorial's oscillator, run offline
```

Put tests like these in `MyPlugin/tests/`, importing your plugin's modules by
name, and run them with RackPy's environment, which has pytest:

```bash
PYTHONPATH=MyPlugin/pythonpath uv run pytest MyPlugin/tests
```

The offline reference is thousands of times slower than the native kernel and
is never what Rack runs, but it computes the same values: arithmetic matches
float32 exactly, and `sin2pi` and `pitch_to_freq` to within one unit in the
last place ([the offline reference](graphs.md#the-offline-reference)).

A kernel written in C++ cannot run offline in Python. Test it as a C++ program
against the SDK, as RackPy's examples do with their C++ references.

## Test events and callbacks on the bench

Rack's module events (reset, randomize, sample-rate changes, ports connecting)
can be driven in Python on RackPy's laboratory bench, offline, one frame at a
time ([the laboratory bench](../explanation/callbacks.md#the-laboratory-bench)).
The bench never runs inside Rack's engine and says nothing about speed: it is
for checking that your handlers do the right thing. The
[state and events example](../../examples/state-and-events/README.md) has a
laboratory script that drives every event.

## Test in Rack

What only Rack can tell you: that the plugin loads, that its models match
`plugin.json`, and that its modules build and save. Build the plugin, start
Rack with a patch that uses your modules, and read the log
([troubleshooting](../troubleshooting.md)):

- `Loaded plugin MyPlugin` and no `RackPy host initialization failed`;
- no `could not create its module` for your models;
- no `Python raised in` during the session.

Quit Rack and look at `autosave/patch.json` in the Rack user folder: your
modules' `data` is what your `to_json` returned. Start Rack again to check that
it comes back.

{py:func}`rackpy.runtime.diagnostics` lists what RackPy recorded while Rack ran
(errors in callbacks, slow callbacks, refused calls). A plugin can log them
itself, from its UI, while you test.
