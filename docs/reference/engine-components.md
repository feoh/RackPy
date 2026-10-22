# Engine components: ports, parameters and lights

`rack::engine`'s `Port`, `Input`, `Output`, `Param`, `Light`, `PortInfo` and
`LightInfo` (RP-13), as `rackpy.raw.engine`. Every method runs Rack's own
inline code; `tests/engine` replays more than 450 cases from a compiled C++
reference (channel counts 0, 1, 2, 5 and 16, normalling, polyphonic sums,
`set_channels` transitions, light smoothing) through the bindings inside Rack.

Modules hand these out (RP-14 binds `Module`). You can also create them
yourself, for tests or for your own native-free helpers.

(components-ownership)=
## Who owns a component

| Created by | Lifetime | Threads |
| --- | --- | --- |
| You (`Port()`, `Light()`, …) | Python owns it, like any object | any |
| A Rack module (`module.inputs[0]`, from RP-14) | the module's: once Rack deletes the module, every use raises `StaleHandleError` | Rack's **main thread only**; another thread gets `ThreadError` |

The main-thread rule is what makes the second row safe. Rack deletes modules on
the main thread, so there a liveness check and the access that follows it cannot
be separated by a deletion. On another thread they could, and the access would
read freed memory, so RackPy refuses instead. Asking twice for the same
component gives the same Python object.

Reading a Rack-owned port or light while the engine runs is unsynchronized,
exactly as Rack's own C++ UI code reads them: a value may come from the current
or the previous engine block. Values are never torn. Python never runs on the
engine thread, so nothing on the audio path creates Python objects or touches a
reference count.

(components-ports)=
## Ports

A `Port` has up to `PORT_MAX_CHANNELS` (16, read from the pinned SDK) voltages
and a channel count: 0 means disconnected, 1 monophonic, more polyphonic.
`Input` and `Output` are `Port`s.

- `get_voltage(channel=0)`, `set_voltage(voltage, channel=0)`,
  `get_poly_voltage(channel)` (channel 0's voltage on a monophonic port),
  `get_normal_voltage(normal, channel=0)` and `get_normal_poly_voltage(normal,
  channel)` (the normal voltage when disconnected) are Rack's methods.
- `get_voltage_sum()`, `get_voltage_rms()`, `clear_voltages()`,
  `read_voltages()` (a list of the first `channels` voltages) and
  `write_voltages(values)` (at least `channels` values; set the count first) act
  on the connected channels only, as in Rack.
- `set_channels(n)` follows Rack's rules: it does nothing on a disconnected port,
  turns 0 into 1, and clears the channels above `n`.
- Facade properties: `voltage` (channel 0), `channel_count`, `connected`,
  `monophonic`, `polyphonic`.

RackPy checks what Rack does not. A channel outside 0 to 15 raises `IndexError`
before Rack indexes its array. A channel count above 16, which would make
Rack's loops run past the array, raises `ValueError`.

The SIMD forms (`getVoltageSimd<T>` and friends) arrive with the SIMD vector
types in RP-20.

(components-voltages)=
## The voltage array: views and copies

`port.voltages` (and `port.get_voltages(first_channel)`) is a checked view of
the port's array: indexing reads and writes the port, with Python-style
negative indices. What you can do with it depends on the owner:

- **A port you own**: `memoryview(port.voltages)` is a zero-copy `float`
  buffer of the port's memory, and the view keeps the port alive.
- **A Rack-owned port**: `memoryview()` raises `BufferError`, because a raw
  buffer could outlive the module and read freed memory. RackPy refuses rather
  than copy behind your back; call `voltages.snapshot()` for an independent
  `array.array('f')` copy.

`port.plug_lights` is a tuple of the three lights Rack draws on a cable's plug
(positive, negative, polyphonic), as views of the port's own lights.

(components-params-lights)=
## Parameters and lights

`Param.get_value()` and `set_value()` read and write the raw parameter value.
`Light` stores the square of a brightness: `set_brightness`, `get_brightness`,
and `set_brightness_smooth(brightness, delta_time, lambda_=30.0)`, which falls
slowly and rises at once. The facade property `brightness` wraps the getter and
setter.

(components-info)=
## PortInfo and LightInfo

Names and descriptions shown in Rack's UI. `PortInfo.get_full_name()` appends
" input" or " output" to `get_name()`. A Python subclass may override
`get_name` and `get_description` (Rack calls them on the UI thread). The
standalone `module` field, a pointer back to the module, is not bound yet
(RP-14B2). Module-owned built-in metadata is currently accessed through distinct,
lifetime-checked `PortInfoView` / `LightInfoView` adapters, not these standalone
classes; see [Module metadata](engine-module.md#built-in-port-and-light-metadata).

(components-deprecated)=
## Deprecated and unstable members

| Member | Rack says | In Python |
| --- | --- | --- |
| `Port.voltages`, `Param.value`, `Light.value` | unstable API | available, documented as unstable; prefer the methods |
| `Port.value`, `Port.channels`, `Port.active` | deprecated (and unstable) | properties that emit `DeprecationWarning`; `channels` is the raw count, written without `set_channels`' rules but still limited to 0 to 16 |
| `Port.normalize`, `Light.set_brightness_smooth(brightness, frames)`, `Light.set_smooth_brightness` | deprecated | work, and emit `DeprecationWarning` |

As in C++, `set_brightness_smooth(0.5)` and `set_brightness_smooth(0.5, 64)`
(an int) reach the deprecated per-frame overload; pass a float `delta_time` for
the current one.
