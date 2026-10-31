# Configuration

An original RackPy plugin with the features of the Plugin API Guide's
[module configuration](https://vcvrack.com/manual/PluginGuide#Module-configuration)
section, and of the [module presets](https://vcvrack.com/manual/Presets) page:
how a module's controls are named, ranged and displayed, how its ports and
lights are named and described, what it does when bypassed, and the presets it
ships. No manual code is copied
([ADR 001](../../docs/decisions/001-licensing-and-identity.md)).

```text
plugin.json, rackpy-plugin.json, .gitignore            the plugin project (bin/rackpy-plugin create)
res/Shaper.svg, res/Splitter.svg                       original panels, with their components layers
pythonpath/rackpyconfiguration/__init__.py             the entry package: adds the models
pythonpath/rackpyconfiguration/shaper.py               every display, a switch, a button, bypass routes
pythonpath/rackpyconfiguration/splitter.py             a custom bypass
pythonpath/rackpyconfiguration_dsp.py                  both modules' processing, as graphs (no Rack)
presets/Shaper/*.vcvm                                  three factory presets
```

## The Shaper

A signal shaper with a tremolo. Its `configure()` shows each way Rack displays
a parameter; hover over a control in Rack to see it:

| Control | Range | Tooltip at the default | Display |
| --- | --- | --- | --- |
| Level | 0 to 2 | `Level: 0 dB` | decibels: base `-10`, multiplier 20, so 20 log10(value) |
| Scale | -1 to 1 | `Scale: 1` | plain and linear |
| Offset | -5 to 5 | `Offset: 0 V` | linear, with a unit |
| Tremolo depth | 0 to 1 | `Tremolo depth: 0%` | a percentage: multiplier 100 |
| Tremolo rate | -3 to 3 | `Tremolo rate: 1 Hz` | exponential: base 2, so 2^value hertz |
| Shape | switch | `Shape: Bipolar` | `config_switch`'s labels: Bipolar, Rectified, Positive half |
| Mute while held | button | `Mute while held` | `config_button`: a momentary switch |

The tremolo rate's tooltip has a description, and so does the output's:
`config_param` and `config_output` return the quantity and port info, whose
`description` Rack shows under the name. `config_input`, `config_output` and
`config_light` name the input "Signal", the output "Shaped" and the light
"Tremolo". `config_bypass(SIGNAL, SHAPED)` gives Rack a bypass route: bypassed,
the module copies its input to its output, channels and all.

The processing is `rackpyconfiguration_dsp.shaper_graph()`: shape, then level,
scale and tremolo, then offset; silent while the button is held; polyphonic
like the input. `configure()` compiles it into a native kernel that Rack's
engine runs every sample. **Python never runs per sample.**

## The Splitter: a custom bypass

The Splitter outputs its input at a level, and inverted. A bypass route only
copies an input to an output, so a bypassed Splitter would put the input
itself, not its inverse, on INVERTED. C++ plugins override `processBypass`
for that; RackPy gives the module a second native kernel,
`module.set_bypass_kernel(...)`, which Rack's engine runs instead of copying
routes while the module is bypassed: here, unity gain with INVERTED still
inverted. See [processing audio](../../docs/how-to/dsp.md#reset-sample-rate-bypass-and-failures).

## Factory presets

`presets/Shaper/` holds three presets, `.vcvm` files of the module's JSON:
its `params` by ID. Rack lists them in the Shaper's **Preset** menu, sorted
by file name with the number prefixes hidden: *Gentle tremolo*, *Full-wave
rectifier*, *Positive half at +6 dB*. Use zero-padded prefixes (`01_`, `02_`):
Rack sorts names as text, so `10_` would come before `2_`. `bin/rackpy-plugin
build` copies `presets/` into the plugin, where Rack looks
(`Model.get_factory_preset_directory()`).

## Build and run

Linux x86_64 with uv, CMake, Ninja and GCC, and Rack Free 2.6.6. From the
repository root:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
bin/rackpy-plugin build examples/configuration --bundle /tmp/rackpy-bundle/RackPy --dest ~/.local/share/Rack2/plugins-lin-x64
```

Start Rack: `log.txt` says `Loaded plugin RackPyConfiguration 2.0.0`, and the
browser lists the Shaper and the Splitter under *RackPy Configuration*. Only
one RackPy plugin runs in a Rack process, so remove other RackPy plugins
first, and remove `plugins-lin-x64/RackPyConfiguration` before building again.

## What the tests check

`tests/examples/test_controls_state.py`:

- offline, the graphs: the Shaper passes, rectifies, halves and mutes its
  input, gives the offset alone when unpatched, and its tremolo spans 0 to 1;
  the Splitter's bypass graph keeps the inversion at unity gain;
- in Rack (test-hooks build), against `native/tests/controls_reference.hpp`,
  the same two modules written in C++ with Rack's `configParam`,
  `configSwitch`, `configButton`, `configInput/Output/Light`, `configBypass`
  and `processBypass` and run inside Rack: every control's name, label, unit,
  description, range, default, snapping, smoothing and switch labels; its
  value, display string and tooltip string at five or six values (the ends,
  the default, a quarter, the middle, and 0 if the range spans it); each port's and
  light's name, full name and description; the bypass routes; and every
  frame of six Shaper and three Splitter scenarios, processed and bypassed,
  mono, polyphonic and unpatched, within 1e-5 V (measured: at most 1e-6 V);
- the factory presets are where Rack looks for them and load;
- built on a production bundle, the plugin loads under its own slug, its
  presets are in the plugin, and its modules survive two launches.

## Restrictions

Linux x86_64 and Rack Free 2.6.6 only, so far. Only one RackPy plugin can run
in a Rack process. The plugin's Python is trusted code, not a sandbox. Do not
distribute development builds. See [the template](../template/README.md#restrictions).
