# Audio

`rackpy.raw.audio` is Rack's `rack::audio`: Rack's audio drivers and
devices, and audio ports that Python owns. **Implemented (RP-29B), Linux
x86_64 inside Rack Free 2.6.6**, for ports and drivers. Implementing a
driver or device in Python, and Python ports' stream callbacks on a real
stream, are RP-29C (see the [coverage report](coverage.md)).

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

- Rack's no-op process callbacks run natively, and `stream_stats` counts
  what passed.
- `on_start_stream` and `on_stop_stream` reach a Python subclass queued, on
  the UI thread.
- A Python override of the process callbacks runs only for a device that
  Python drives itself (RP-29C).

Rack's audio drivers outlive its context but not RackPy's interpreter. Every
Python port therefore gives its device back when Rack's context closes, and
is stale from then on. A port Python drops is deleted on the main thread.

`Port.getDeviceDetail` is declared in Rack 2.6.6's header but defined
nowhere: libRack has no symbol for it, so no plugin can call it.

(audio-devices)=
## Devices

A device belongs to its driver (RtAudio deletes one when its last port
leaves). Python reaches it only through the port that holds it, and the view
goes stale once the port lets go. It has Rack's queries and setters for
name, channels, sample rate and block size. `subscribed` lists the device's
subscribers, Python's own ports and read-only `PortView`s of the others,
under Rack's `processMutex`. `subscribe` and `unsubscribe` are Rack's
lower-level calls, which RackPy undoes with the port.
