# Audio

`rackpy.raw.audio` is Rack's `rack::audio`: Rack's audio drivers and
devices, and audio ports that Python owns. **Implemented (RP-29B, RP-29C), Linux
x86_64 inside Rack Free 2.6.6**, apart from `Port.getDeviceDetail`, which
Rack declares but never defines (see the [coverage report](coverage.md)).

(audio-drivers)=
## Rack's drivers

`get_driver_ids()` lists Rack's audio drivers, and `get_driver(driver_id)`
returns one as a `Driver`, which lives until Rack quits. Rack Free on Linux
has RtAudio's ALSA, PulseAudio and JACK. A driver lists its devices, their
names, its default device and their channel counts. `subscribe(device_id,
port)` makes a Python port a subscriber of a device, apart from the port's
own device. RackPy undoes it when the port dies or the context closes.
Rack's RtAudio driver raises Rack's `Exception` for a device it cannot open.

RackPy's tests isolate Rack from the machine's sound servers and devices, so
there these drivers list no devices. Physical devices are not tested.

(audio-ports)=
## Ports

`Port()` is Rack's audio port, as Core's Audio modules use, owned by Python,
on the main thread in Rack's live UI. Like Rack's, a new port takes Rack's
first driver and its default device, which may open a real stream. A port
has Rack's methods:

- `set_driver_id` falls back to Rack's first driver for an unknown ID.
- `set_device_id` takes a device; a device the driver cannot open is logged
  and left unset, as Rack does.
- `get_device_ids`, `get_device_name`, `get_device_num_inputs` and
  `get_device_num_outputs`.
- `get_sample_rates` and `get_block_sizes` return sorted lists, and
  `get_sample_rate`, `set_sample_rate`, `get_block_size` and
  `set_block_size` act on the port's device; without one, Rack does nothing.
  The setters release the GIL, because the driver may reopen its stream.
- `get_num_inputs` and `get_num_outputs` count the port's active channels
  after `input_offset`/`output_offset` and `max_inputs`/`max_outputs`, which
  are writable.
- `to_json` and `from_json` save and restore the driver, the device (by
  name), its rate and block size, and the offsets.

A device calls its ports' `process_input`, `process_buffer` and
`process_output` on the audio driver's thread, and `on_start_stream` and
`on_stop_stream` there or where the port subscribes. Python never runs on
that thread:

- For an ordinary port, Rack's no-op process callbacks run natively, and
  `stream_stats` counts what passed. A port attached to a RackPy module
  instead routes device frames through the module's native I/O bridge.
- `on_start_stream` and `on_stop_stream` reach a Python subclass queued, on
  the UI thread.
- A Python override of the process callbacks runs only for a device that
  Python drives itself ([Python drivers](#audio-python-drivers)), on
  that thread. It gets copies of its part of the buffers, and its writes to
  the output reach the device.

(audio-module-io)=
### Module audio I/O (RP-31J, work in progress)

`Module.set_audio_port(port, inputs=..., outputs=...)` attaches a Python-owned
port to a module before Rack's engine owns the module. The port must not
override the process callbacks; the channel limits are fixed while attached.
Its native bridge connects the device to a kernel or
[graph](../how-to/graphs.md#audio-and-midi), without running Python for
samples. The built-in `AudioInterface` kernel implements Core Audio's
10-volt conversion and sums polyphonic inputs. `audio_stats` exposes buffer
and resampler counters. Detach by setting the port to `None` while Python owns
the module; if the port goes first, the module hears silence.

The bridge can be Rack's audio master or a secondary port. As master, it
claims the engine when no master is present, suggests the device's sample rate,
and calls Rack's public `Engine::stepBlock` on the driver thread, as Core
Audio does. `stepped_frames` counts frames it clocks as master. Stopping the
stream releases its master role; a restarted stream can claim it again.

The Linux live-Rack comparison covers steady-rate operation and characterizes
initial rate changes, not every startup or transition ordering. UI maintenance
prepares for a newly requested fixed engine rate in the range 11.025–768 kHz
when it sees the request before Rack applies it. A separate handover slot
preserves the converter needed by the currently applied rate. If a required
resampler has not been prepared, or the future slot is being replaced at the
moment the driver needs it, the bridge can produce counted silent blocks
(`resampler_misses`). It does not wait for the UI thread and is not equivalent
to Core's synchronous converter creation. Rack 2.6.6's
`audio::Device::processBuffer` itself takes `processMutex` before calling ports;
Rack's engine, an external master's driver, and device getters may also use
locks. The exchange guard does not prove the complete host audio path
lock-free. No physical audio device, macOS or Windows has been tested for this
path.

Rack's audio drivers outlive its context but not RackPy's interpreter. Every
Python port therefore gives its device back when Rack's context closes, and
is stale from then on. A port Python drops is deleted on the main thread.

`Port.getDeviceDetail` is declared in Rack 2.6.6's header but defined
nowhere: libRack has no symbol for it, so no plugin can call it.

(declared-but-undefined)=
### Declared but undefined

`Port::getDeviceDetail(deviceId, offset)` is declared in Rack 2's
`audio.hpp`, but Rack 2 never defines it: libRack exports no such function,
so no plugin can call it, and RackPy does not bind it. It is a leftover of
Rack 1, where it labelled an RtAudio device for Rack's own audio widget. Rack
2 builds that label from `get_device_name`, `get_num_inputs`,
`get_num_outputs` and the port's offsets, which RackPy binds. No published
plugin calls it either.

(audio-devices)=
## Devices

A device belongs to its driver (RtAudio deletes one when its last port
leaves). Python reaches it only through the port that holds it, and the view
goes stale once the port lets go. It has Rack's queries and setters for
name, channels, sample rate and block size. `subscribed` lists the device's
subscribers, Python's own ports and read-only `PortView`s of the others,
under Rack's `processMutex`. `subscribe` and `unsubscribe` are Rack's
lower-level calls, which RackPy undoes with the port.

(audio-python-drivers)=
## Python drivers and devices

:::{warning}
Native configured answers and queued requests below are not synchronous
Python virtual overrides. A Python subclass overrides the driver and device
virtuals only in the
[laboratory](../explanation/callbacks.md#laboratory-audio-and-midi-drivers)
(`rackpy.laboratory.AudioDriver` and `AudioDevice`), which is offline,
explicitly enabled, main-thread only and not production real-time audio;
see the [callback inventory](callback-paths.md). The five port stream
callbacks have a synchronous path on Python-driven devices only, never
Rack's driver threads.
:::

Python implements an audio driver as a natively configured `PythonDriver`
(RP-29C). Rack calls a driver's and a device's methods from driver threads
and the engine thread; Core's Audio module, for one, reads a device's
channel counts and rate on the audio thread. So native code answers every
call from values Python set, and Python drives the stream from its own
thread:

```{rackpy-example} virtual_audio.py
```

- `PythonDriver(name)` holds devices: `add_device(name, num_inputs,
  num_outputs, sample_rates, sample_rate, block_sizes, block_size)` returns a
  `PythonDevice` with a stable ID. `add_driver(driver_id, driver)` gives it
  to Rack, which owns it until it quits. The ID is stored in patches, so -1
  and taken IDs are refused.
- A `PythonDevice`'s `name`, channel counts, rates and block sizes are
  native values that Rack reads on any thread. Rack's `setSampleRate` and
  `setBlockSize` apply the value and record a request, which
  `take_requests()` returns on the main thread.
- `start_stream()`, `process_buffer(input, input_stride, output,
  output_stride, frames)` and `stop_stream()` are Rack's driver-side calls,
  run on the calling Python thread with the GIL released. The buffers are
  interleaved float32 frames, and `process_buffer` overwrites the output.
  Rack's ports run as they would on an audio thread. Core's Audio module
  claims the master module and steps Rack's engine by the device's blocks,
  and a Python port's overrides run synchronously.
- `remove_device(device)` unplugs it: its stream stops and Rack's lists no
  longer show it. Rack's ports keep it until they choose another device, as
  with RtAudio, and it is deleted when the last one leaves. A removed device
  refuses to stream.

The `rackpy.audio` facade finds drivers by name (`drivers`, `find_driver`),
opens ports (`open_port(driver, device, kind=...)`), makes and registers a
virtual driver (`virtual_driver`), and drives a device one block at a time
(`Stream`).
