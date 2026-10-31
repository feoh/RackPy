# Use audio and MIDI devices

Rack talks to sound cards and MIDI devices through **drivers**, **devices**
and **ports**, as its Core *Audio* and *MIDI-CV* modules do. This guide shows
which way to use each, depending on whether the data must follow the audio or
only reach your Python.

**Status:** Linux x86_64, Rack Free {{rack_version}}. MIDI and Python-driven
audio devices are tested in Rack. A module's own audio port is **work in
progress**: tested on Rack's loopback and virtual devices, never with a
physical sound card ([audio](../reference/audio.md#module-audio-io-rp-31j-work-in-progress)).

| You want | Use | Python runs |
| --- | --- | --- |
| Notes and controller messages in your Python (a menu, a display, a recorder) | a `midi.Input` subclass's `on_message` | on Rack's main thread, after the message arrives |
| MIDI turned into voltages, sample-accurately | an `InputQueue` attached to the module, and a `MidiToCV` kernel or `midi_cv` graph node | never; native |
| A module that is itself an audio interface | `Module.set_audio_port` and an `AudioInterface` kernel or `audio_input`/`audio_output` graph nodes | never; native |
| Send MIDI | `midi.Output.send_message` | where you call it |
| A device of your own, fed from Python | `rackpy.audio.virtual_driver` and `rackpy.audio.Stream` | on the thread that calls `Stream.process` |

## Receive MIDI in Python

Subclass `midi.Input` and override `on_message`. Rack delivers MIDI on its
driver's thread, where Python never runs, so RackPy queues each message and
calls `on_message` on Rack's main thread at its next frame. A full queue drops
the message and counts it, rather than block the MIDI thread.

```{rackpy-example} midi_loopback.py
:caption: A port that collects the notes it receives, on Rack's Loopback driver
```

## Turn MIDI into voltages

For a gate or pitch that must change at the right sample, the messages must
reach native code, not Python. Attach an empty, disconnected
`midi.InputQueue` to the module with `set_midi_input(index, queue)` before
Rack's engine owns it, connect the port to a device, and give the module a
kernel that reads it: the built-in `rackpy.dsp.kernel.MidiToCV`, which has
Core MIDI-CV's twelve outputs, or a graph's `midi_cv` node
([graphs](graphs.md#audio-and-midi)). Messages longer than three bytes,
SysEx included, are dropped ([MIDI](../reference/midi.md#ports)).

## Be an audio interface

`Module.set_audio_port(port, inputs=..., outputs=...)` attaches an audio port
to a module, and its kernel then exchanges samples with the device every
frame: the built-in `AudioInterface` kernel converts as Core Audio does
(a device's full scale is 10 V), or a graph uses `audio_input` and
`audio_output`. As Rack's only audio module, it can clock Rack's engine, as
Core Audio does. The [audio reference](../reference/audio.md#module-audio-io-rp-31j-work-in-progress)
lists what is and is not established yet.

## Feed a device from Python

A Python-driven device is a device whose samples Python produces or consumes:
for tests, offline rendering, or bridging to something Rack has no driver
for. Python runs on whichever thread calls `Stream.process`, never on a Rack
audio thread:

```{rackpy-example} virtual_audio.py
:caption: A virtual device streams a ramp to a port that records each block's peak
```

## Rules for every port

- Ports are made and changed on Rack's main thread, while Rack's window is
  open: a port needs Rack's engine for timing.
- A port saves its driver and device in `to_json` and finds the device again
  by name in `from_json`. Save it with your module's state.
- Rack's drivers outlive the patch: RackPy gives every Python port's device
  back when Rack closes, and a port you drop is deleted on the main thread.
