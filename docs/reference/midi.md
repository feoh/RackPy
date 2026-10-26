# MIDI

`rackpy.raw.midi` is Rack's `rack::midi`: MIDI messages, Rack's MIDI
drivers and devices, and MIDI ports that Python owns. **Implemented
(RP-29A, RP-29C), Linux x86_64 inside Rack Free 2.6.6** (see the
[coverage report](coverage.md)).

```{rackpy-example} midi_loopback.py
```

(midi-messages)=
## Messages

A `Message` is a value: `bytes` (three zeros at first, as a list of 0 to
255) and `frame`, the engine frame it belongs to (-1: now). The accessors
are Rack's:

- `get_status`/`set_status` and `get_channel`/`set_channel` read and write
  the two halves of the first byte.
- `get_note`/`set_note` and `get_value`/`set_value` read and write the next
  two bytes. The setters mask to 7 bits.
- A missing byte reads as 0, and setting it does nothing (`set_size`
  changes the size).
- `to_string()` is the bytes in hex.

(midi-drivers)=
## Rack's drivers

`get_driver_ids()` lists Rack's MIDI drivers, and `get_driver(driver_id)`
returns one as a `Driver`, which lives until Rack quits. Rack Free has these:

- Loopback (-12), whose 16 devices pass what is sent on device N to the
  inputs on device N.
- Computer keyboard/mouse.
- Gamepad.
- RtMidi's, such as ALSA and JACK, depending on the machine.

A driver lists its input and output devices and their names and defaults.
`subscribe_input(device_id, port)` and `subscribe_output` make a Python port
a subscriber of a device, apart from the port's own device. RackPy undoes
such a subscription when the port dies, and `unsubscribe_*` takes back only
the subscriptions Python made.

(midi-ports)=
## Ports

`Input()`, `InputQueue()` and `Output()` are Rack's ports, owned by Python,
on the main thread in Rack's live UI. They need Rack's engine to timestamp
messages, so making one at plugin start-up raises `ContextError`. As in
Rack, a new port takes Rack's first driver and its default device. A port
has Rack's methods:

- `set_driver_id` falls back to Rack's first driver for an unknown ID, then
  takes its default device.
- `set_device_id` takes a device, and -1 means none.
- `get_device_ids`, `get_device_name`, `get_channels`,
  `get_channel_name`, `to_json`, and `from_json`, which finds the saved
  device by name.
- `channel` is an input's channel filter, or the channel an output sets on
  each message (-1: all, or leave it).

`get_device_ids`, `get_default_device_id`, `set_device_id`,
`get_device_name` and `get_channels` are Rack's virtuals. A Python `Input`
or `Output` subclass may override them, and Rack's menus and its
`set_driver_id` then call the override on the UI thread.

- **`Input`.** Rack delivers messages on the MIDI driver's thread, or, for
  the Loopback driver, on whichever thread sends, often an engine thread.
  Python never runs there. The native port copies each message into a queue
  of 1024, and RackPy calls the subclass's `on_message(message)` on the UI
  thread at its next step. The queue never blocks the sending thread: a
  message that finds it full or busy is dropped and counted in `dropped`.
  An unset frame is set from Rack's engine timing, as Rack does.
- **`InputQueue`** is Rack's queue port. It keeps up to 8192 messages in
  frame order, and `try_pop(max_frame)` returns the next one due, or `None`.
- **`Output.send_message(message)`** sends to the port's device, setting the
  port's channel unless it is -1.

Rack's drivers outlive its context but not RackPy's interpreter. Every
Python port therefore gives up its devices when Rack's context closes, and
is stale from then on. A port Python drops is deleted on the main thread.

(midi-devices)=
## Devices

A device belongs to its driver; Rack's RtMidi driver deletes one when its
last port leaves. Python reaches a device only through the port that holds
it (`port.device`, `input_device`, `output_device`, or a driver's
`subscribe_*`), and the view goes stale once the port lets go.

- `get_name()` is the device's name.
- `subscribed` lists the device's subscribers: Python's own ports, and
  read-only `PortView`s of the others, such as modules' ports.
- `InputDevice.on_message(message)` delivers a message to the subscribers,
  as the driver does when one arrives.
- `OutputDevice.send_message(message)` sends it as it is, without applying
  any channel.
- `subscribe` and `unsubscribe` are Rack's lower-level calls, which Rack
  says are not meant for plugins. RackPy undoes them with the port, as it
  does a driver's.

(midi-python-drivers)=
## Python drivers and devices

Python implements a MIDI driver as a natively configured `PythonDriver`
(RP-29C), whose calls native code answers:

- `add_input_device(name)` returns a `PythonInputDevice`. Its
  `push_message(message)` is Rack's `InputDevice::onMessage`, as a driver
  calls it when a message arrives: every subscribed input gets it, filtered
  by channel and timestamped by Rack's engine if its frame is unset.
- `add_output_device(name)` returns a `PythonOutputDevice`. Rack's ports
  send to it from engine threads (modules' `process()`), so it only queues
  each message, without blocking, in a queue of 1024 that counts drops in
  `dropped`. `take_sent()` returns the queued messages, in order, on the main
  thread.
- `remove_input_device`/`remove_output_device` unplug a device. Rack's lists
  no longer show it, the ports that hold it keep it until they move, and it
  is deleted when the last one leaves. A removed input refuses pushes.
- `add_driver(driver_id, driver)` gives the driver to Rack, which owns it
  until it quits. Taken IDs are refused.

The `rackpy.midi` facade builds messages (`note_on`, `note_off`,
`control_change`, `message`, with range checks), finds drivers by name
(`drivers`, `find_driver`), opens ports by driver and device name
(`open_input`, `open_output`), and makes a virtual driver with its devices
(`virtual_driver`).
