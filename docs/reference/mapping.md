# Raw bindings and the Python facade

This development reference is a guide to finding the layers, not a claim that
all supported Rack APIs or platform semantics are verified. The authoritative
per-declaration status is the [coverage report](coverage.md) and its linked
inventory. A facade is optional: its omissions do not remove declarations from
the coverage denominator.

## Layers

- Rack's public C++ SDK surface starts at `rack.hpp`.
- `rackpy.raw` exposes bindings organized by the Rack namespaces, with
  snake_case Python spellings. Consult the relevant family reference for
  overloads, availability, lifetime, thread and callback restrictions.
- Other `rackpy` modules provide convenient imports or adapters. These are
  additional entry points, not substitutes for raw semantics. Some wrap a raw
  operation; others re-export the same bound class.

Most raw and facade modules require a running RackPy host inside Rack. The
pure-Python graph description can be built outside Rack, but compiling or
running it as a native kernel requires the host. Neither facade callbacks nor
releasing the GIL makes Python suitable for the production audio thread.
See [callback rules](callback-matrix.md) and the [DSP reference](dsp.md).

## Starting points

| Rack surface | Raw entry point | Facade or adapter | Notes |
| --- | --- | --- | --- |
| `rack::math` | `rackpy.raw.math` | `rackpy.math` | Re-exports value types and functions; see [values](values.md) for live views and copies. |
| `rack::engine` components | `rackpy.raw.engine` | `rackpy.engine` | Re-exports bound component classes; [engine components](engine-components.md) describes ownership. |
| `rack::engine::Engine`, `rack::history` | `rackpy.raw.engine`, `rackpy.raw.history` | `rackpy.engine.host` | Explicit host lookup and helpers; operations remain subject to Rack's ownership and threading rules. |
| `rack::plugin` | `rackpy.raw.plugin` | `rackpy.plugin` | Plugin and model registration helpers; see [plugin](plugin.md). |
| `rack::widget`, NanoVG | `rackpy.raw.widget`, `rackpy.raw.nanovg` | `rackpy.widget`, `rackpy.drawing` | `WidgetNode` is a checked adapter, not a replacement for subclassable raw widgets. |
| `rack::audio` | `rackpy.raw.audio` | `rackpy.audio` | Driver/device helpers and `Stream` for Python-driven devices; see [audio](audio.md). |
| `rack::midi` | `rackpy.raw.midi` | `rackpy.midi` | Message and driver helpers; production native input has a documented three-byte limit. See [MIDI](midi.md). |
| `rack::dsp` | `rackpy.raw.dsp` | `rackpy.dsp.graph` | Graphs build native processing plans, not arbitrary Python audio callbacks. |
| `rack::asset` | `rackpy.raw.asset` | `rackpy.assets` | Facade returns `pathlib.Path` objects. |
| `rack::system`, `rack::network` | `rackpy.raw.system`, `rackpy.raw.network` | `rackpy.services.system`, `rackpy.services.network` | Synchronous operations; consult [services](services.md) for safety restrictions. |

`rackpy.plugin.ModuleClass` lifecycle callbacks accept the corresponding
nested `rackpy.raw.engine.Module` event types. They retain the thread and
ownership constraints described in [plugin](plugin.md). The strict typing
contract exercises these signatures without running callbacks outside Rack.

For complete lookup, use the [surface inventory](surface-summary.md) and the
family reference pages rather than assuming a declaration appears in this
short table. [Facade contract and versioning](api-stability.md) defines which
names are public and how changes to them are controlled; neither page implies
production readiness.
