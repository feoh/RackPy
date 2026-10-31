# Coming from Rack's C++ API

If you know Rack's C++ plugin API, most of RackPy is the same API with Python
spellings: `rack::app::ModuleWidget` is `rackpy.raw.app.ModuleWidget`,
`configParam` is `config_param`, and `createParamCentered<RoundBlackKnob>` is
`create_param_centered(cl.RoundBlackKnob, ...)`. This page lists what changes
beyond spelling, and what to use instead of Rack's C++ toolchain.

**Status:** Linux x86_64, Rack Free {{rack_version}}. Every correspondence
below is bound and tested; the [coverage report](../reference/coverage.md)
has each declaration's status, and the [C++ → Python crosswalk](../reference/crosswalk.md)
its Python name.

## Names

- Namespaces become modules: `rack::engine` is `rackpy.raw.engine`,
  `rack::componentlibrary` is `rackpy.raw.componentlibrary`, and so on
  ([raw bindings and the facade](../reference/mapping.md)).
- `camelCase` becomes `snake_case`: `getVoltage` is `get_voltage`,
  `onSampleRateChange` is `on_sample_rate_change`.
- Templates take the type as their first argument:
  `createWidget<ScrewSilver>(pos)` is `create_widget(cl.ScrewSilver, pos)`.
- Fields stay fields (`port.channels`, `box.pos`), with Rack's own deprecated
  ones warning as Rack's compiler would.

## A module

| C++ | RackPy |
| --- | --- |
| `struct MyModule : Module` with a constructor that calls `config(...)` and `configParam(...)` | a function `configure(module)`, or a `ModuleClass` whose `configure()` does the same ([plugins](plugins.md)) |
| `void process(const ProcessArgs&)` | a [graph](graphs.md) or a [kernel](dsp.md) that Rack's engine runs natively; never Python |
| `dataToJson` / `dataFromJson` | `to_json` / `from_json` returning and receiving Python values ([state](state.md)) |
| `onReset`, `onRandomize`, `onAdd`, `onSave`, ... | `on_reset`, `on_randomize`, `on_add`, `on_save`, ... ([module events](plugins.md#module-events)) |
| `onSampleRateChange` | reaches the kernel natively; Python cannot handle it, because Rack can send it from an audio thread |
| `leftExpander`, `rightExpander` | the same, with RackPy's message buffers ([expanders](expanders.md)) |
| `Model* modelX = createModel<X, XWidget>("X")` and `p->addModel(modelX)` | `plugin.add_model("X", configure, widget=XWidget)` in the entry package |

The important difference is `process()`. In C++ it is your code, called every
sample. In RackPy, Python describes the processing once and native code runs
it, because Python cannot run reliably 48,000 times a second
([the tutorial](../tutorials/oscillator.md#python-configures-native-code-processes)).
Loops that a graph cannot express go in a kernel written in C++, in the same
plugin.

## A widget

Module widgets, panels, controls, lights, menus and drawing are classes and
functions with the same names and arguments as in C++
([drawing](draw-panels.md), [menus](menus.md)):

| C++ | RackPy |
| --- | --- |
| `setPanel(createPanel(asset::plugin(pluginInstance, "res/X.svg")))` | `self.set_panel(raw.create_panel(str(assets.plugin("res/X.svg"))))` |
| `addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x, y)), module, ID))` | `self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(x, y)), module, ID))` |
| `void appendContextMenu(Menu* menu)` | `def append_context_menu(self, menu)` |
| `void draw(const DrawArgs& args)` with `nvg...` calls | `def draw(self, args)` with `rackpy.raw.nanovg` |

Subclassing works as in C++: override a virtual method, and call
`super().method(...)` where C++ calls the base class.

## Pointers become handles

C++ hands out raw pointers to modules, widgets and ports. Python gets handles:
when Rack deletes the object, the handle goes **stale** and raises
{py:class}`~rackpy.errors.StaleHandleError` instead of reading freed memory.
Asking for the same object twice gives the same Python object. Look objects up
again rather than keeping them across patches ([the runtime](../runtime.md#handles-to-rack-objects)).

Where C++ takes a pointer to a field (a menu item's `bool*`), RackPy takes any
object with a `value` attribute.

## Threads

C++ code may run on Rack's engine and audio threads. Python never does: RackPy
refuses, and records it. Python runs on Rack's main thread, where Rack builds
modules, draws, and handles menus and events, and on threads you start
([callbacks](../explanation/callbacks.md)).

## Rack's toolchain, and RackPy's

| Rack's C++ workflow | RackPy |
| --- | --- |
| Install MSYS2 (Windows), Homebrew packages (macOS) or a distribution's build packages (Linux) | Linux x86_64 only, so far: uv, CMake, Ninja, GCC and git; RackPy downloads the SDK and Python itself ([quickstart](../quickstart.md#what-you-need)) |
| Download the SDK and set `RACK_DIR` | `bin/rackpy-dev build` uses the pinned, checksummed SDK |
| `helper.py createplugin` and `createmodule` | `bin/rackpy-plugin create` and `add-module` ([template](../../examples/template/README.md)) |
| Clone a plugin, `make dep`, `make`, `make install` | `bin/rackpy-plugin build` combines your project with RackPy's bundle; there is no Makefile |
| `make dist` | not yet: distribution packages are RackPy's release work ([packaging](packaging.md)) |

RackPy targets Rack 2 only. There is nothing of Rack 1 to migrate: a new
project's `plugin.json` already has a Rack 2 version (`2.0.0` unless you pass
`--version`), and rebuilding is `bin/rackpy-plugin build` again, after
deleting the previous plugin folder.
