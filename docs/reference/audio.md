# Audio

`rackpy.raw.audio` is Rack's `rack::audio`: Rack's audio drivers and
devices, and audio ports that Python owns. **Implemented (RP-29B, RP-29C), Linux
x86_64 inside Rack Free 2.6.6**, apart from `Port.getDeviceDetail`, which
Rack declares but never defines (see the [coverage report](coverage.md)).

(audio-drivers)=
## Rack's drivers

`get_driver_ids()` lists Rack's audio drivers, and `get_driver(driver_id)`
returns one as a `Driver`, a view by ID. Python never deletes a driver:
Rack deletes every one, its own and Python's, in its `audio::destroy()` as
it quits, after closing its context and before the plugins' `destroy()`.
A {ref}`destroy callback <plugin-hooks>` therefore finds a
`PythonDriver` handle and its devices stale; a test in Rack checks this.
Rack Free on Linux has RtAudio's ALSA, PulseAudio and JACK. A driver lists
its devices, their names, its default device and their channel counts.
`subscribe(device_id, port)` makes a Python port a subscriber of a device,
apart from the port's own device. RackPy undoes it when the port dies or
the context closes.
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
- `get_driver`, `get_driver_id` and `get_driver_name` return the port's
  driver, its ID and its name; `get_device_id` returns its device's ID (-1:
  none) and `get_device` the `Device` view, or `None`. The properties
  `driver_id`, `device_id` and `device` are the same.
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

- Rack's no-op process callbacks run natively, and `stream_stats` counts
  what passed.
- `on_start_stream` and `on_stop_stream` reach a Python subclass queued, on
  the UI thread.
- A Python override of the process callbacks runs only for a device that
  Python drives itself ([Python drivers](#audio-python-drivers)), on
  that thread. It gets copies of its part of the buffers, and its writes to
  the output reach the device.

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
  with RtAudio, and it is deleted when the last one leaves: its `alive` is
  then false. A removed device refuses to stream.

The `rackpy.audio` facade finds drivers by name (`drivers`, `find_driver`),
opens ports (`open_port(driver, device, kind=...)`), makes and registers a
virtual driver (`virtual_driver`), and drives a device one block at a time
(`Stream`).
