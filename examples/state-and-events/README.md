# State and events

An original RackPy plugin with the features of the Plugin API Guide's
[module events](https://vcvrack.com/manual/PluginGuide#Module-events) and
[data serialization](https://vcvrack.com/manual/PluginGuide#Data-serialization)
sections, and of the migration guide's notes on reset, randomize and patch
storage: what a module does on Initialize, Randomize and a sample-rate change,
and the three places its state can live. No manual code is copied
([ADR 001](../../docs/decisions/001-licensing-and-identity.md)).

```text
plugin.json, rackpy-plugin.json, .gitignore      the plugin project (bin/rackpy-plugin create)
res/Sequencer.svg                                the original panel, with its components layer
pythonpath/rackpystateevents/__init__.py         the entry package: plugin settings, the model
pythonpath/rackpystateevents/sequencer.py        the module (a ModuleClass) and its widget
pythonpath/rackpystateevents_core.py             the graph and the saved formats (no Rack)
laboratory/events_on_a_bench.py                  every event in Python, offline
```

## The Sequencer

Sixteen steps, each a voltage between 0 and 1 times the module's range. A
rise at CLOCK (above 1 V, after falling below 0.1 V) moves to the next step,
wrapping after LENGTH steps; a rise at RESET returns to the first. CV is the
current step's voltage, and the light flashes for 50 ms at each clock. The
counting is `rackpystateevents_core.sequencer_graph()`, compiled into a native
kernel; Python hands it the step values and the range as controls, and never
runs per sample.

`Sequencer` is a `rackpy.plugin.ModuleClass`: Rack's module gets one instance,
which holds the module's Python state for as long as the module exists.

## Three places for state

| State | Where | How |
| --- | --- | --- |
| LENGTH | the patch, as a parameter | Rack saves parameters itself |
| the range (0-1, 0-2, 0-5 or 0-10 V) | the patch, as the module's JSON | `to_json` returns `{"version": 1, "range": n}`; `from_json` reads it back |
| the sixteen step values | the module's patch storage, `pattern.f32` | `on_save` writes the file; `on_add` reads it |
| the length Initialize sets | Rack's `settings.json`, for the plugin | `plugin.set_settings_handlers` in the entry package |

Small state belongs in the JSON; large or binary data (samples, tables) in
patch storage, a directory Rack gives each module next to the patch and packs
into a `.vcv` file. Choose the range and the default length from the module's
context menu (right-click): **Range** and **Length after Initialize**.

Everything read back is untrusted: patch files and `settings.json` can be
edited, and patches come from other people. The readers in
`rackpystateevents_core` take only values they understand. A missing field
keeps its value; a newer version's data is read for the fields this version
knows; a pattern file of the wrong size is ignored; NaN reads as 0 and step
values are clamped to [0, 1]. Nothing in a patch runs as code.

## Events

- **Initialize** (`on_reset`): Rack resets the parameters first, except
  LENGTH, whose `reset_enabled` is off; the handler restores the default
  range and pattern and sets LENGTH to the plugin's default length. RackPy
  resets the kernel natively, so the sequence restarts at the first step.
- **Randomize** (`on_randomize`): Rack randomizes the parameters it may
  (LENGTH's `randomize_enabled` is off); the handler draws a new pattern and
  sets LENGTH to 4, 8, 12 or 16, rather than to any length.
- **Sample-rate change**: Rack sends it on an audio thread, where RackPy never
  runs Python. The kernel gets it natively: the light's 50 ms pulse follows
  the new rate.

Both handlers take the event Rack passes, `Module.ResetEvent` and
`Module.RandomizeEvent`. The manual's randomize example overrides
`onRandomize` with a `ResetEvent`, which does not compile; see
[the errata](../../docs/licensing.md#errata-found-while-classifying).

To see every event, the sample rate's included, handled in Python, run
`laboratory/events_on_a_bench.py` inside Rack (trusted code). It puts a
laboratory module on a `rackpy.laboratory.Bench`, an offline host that Python
steps on Rack's main thread and that dispatches events as Rack's engine does.
Its glide recomputes a per-sample coefficient when the rate changes, so it
reaches 63% of a step in 10 ms at both 48 and 96 kHz. A bench is not
real-time audio.

## Build and run

Linux x86_64 with uv, CMake, Ninja and GCC, and Rack Free 2.6.6. From the
repository root:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
bin/rackpy-plugin build examples/state-and-events --bundle /tmp/rackpy-bundle/RackPy --dest ~/.local/share/Rack2/plugins-lin-x64
```

Start Rack: `log.txt` says `Loaded plugin RackPyStateEvents 2.0.0`. Patch a
clock into CLOCK and CV to an oscillator's pitch; Randomize the module, save
the patch, and reopen it to hear the same sequence. Only one RackPy plugin runs
in a Rack process, so remove other RackPy plugins first, and remove
`plugins-lin-x64/RackPyStateEvents` before building again.

## What the tests check

`tests/examples/test_controls_state.py`:

- offline: the graph steps, wraps at LENGTH, restarts on RESET and scales by
  the range; the JSON, pattern-file and settings readers accept what they
  should and ignore wrong types, out-of-range values, missing fields, unknown
  fields, wrong-size files and NaN;
- in Rack (test-hooks build): LENGTH's flags; the JSON round trip; one step
  per clock rise; in Rack's engine, `on_save` writing the pattern file during
  Rack's save, Randomize choosing a pattern and a length, Initialize restoring
  the defaults and the plugin's default length; the laboratory script;
- in windowed Rack, through two launches: a patch whose sequencers have valid
  data, a newer version's data, and none, with a good pattern file, one of
  the wrong size and none, and plugin settings in `settings.json`, loads as
  described above; the changes made in the first launch (a range, a
  randomized pattern and length, the default length) are in the saved patch,
  its storage and `settings.json`, and come back in the second;
- built on a production bundle, the plugin loads under its own slug and its
  module survives two launches.

## Restrictions

Linux x86_64 and Rack Free 2.6.6 only, so far. Only one RackPy plugin can run
in a Rack process. The plugin's Python is trusted code, not a sandbox. Do not
distribute development builds. See [the template](../template/README.md#restrictions).
