# RP-29 bounded implementation slices

RP-29 (`tk-rackpy-29-bind-audio-and-midi-device-port-and-dr-b85853`) binds
`rack::audio` and `rack::midi`. It is split into bounded children, each
closed with a commit on `main`. Decisions follow the owner's standing
instruction to take the recommended option.

RP-06 left an open decision: the driver and device virtuals were classified
"unresolved". **Decision:** Python may implement drivers and devices only as
natively configured objects. Rack calls their virtuals from driver and
engine threads; Core's Audio module, for one, reads `Device::getNumInputs`
and `getSampleRate` in `processInput` on the audio thread. So a Python
driver declares its devices and values, and native code answers every
virtual. Python drives its streams from its own thread with the GIL
released, and Rack's requests reach Python as queued UI-thread
notifications through bounded native queues.

| Slice | Scope |
| --- | --- |
| 29A | MIDI messages, Rack's MIDI drivers and devices (views), Python-owned Input/InputQueue/Output ports |
| 29B | Audio ports with Rack's audio drivers (views) |
| 29C | Python-configured audio and MIDI drivers and devices (`addDriver`): the deterministic fake-device fixture; `rackpy.audio`/`rackpy.midi` facades; callback-rules update |

**RP-29A — MIDI messages, ports and Rack's MIDI drivers**
(`tk-rackpy-29a-midi-messages-ports-and-rack-s-midi-d-48837f`). Validation:
[rp-29a-validation.md](rp-29a-validation.md).

- **Messages.** `Message` is a value with Rack's byte rules.
- **Drivers.** Rack's drivers are `Driver` views by ID; drivers live until
  `midi::destroy()`.
- **Devices.** A device is reached through the Python port that holds it,
  because Rack's RtMidi driver deletes devices. Subscribers Python didn't
  make are `PortView`s, found again in the device's set.
- **Ports.** Python's ports are trampolines (`TrackedPort`) in a registry.
  Each records its subscriptions outside `setDeviceId` and releases them all
  at context close: Rack runs `midi::destroy()` before `plugin::destroy()`,
  which is when RackPy finalizes. A dropped port is deleted on the main
  thread (`MainThreadDelete` holders).
- **`Input.onMessage`** runs on driver or engine threads. The native port
  copies messages into a 1024-slot ring under try-lock, so the sending
  thread never blocks and drops are counted. The Python override runs on
  the UI thread from RackPy's UI step.
- **Port virtuals.** `Port`'s virtuals are overridable in Python `Input`
  and `Output` subclasses, through `overrideOr` on the UI thread. A port's
  own release calls Rack's base `setDeviceId`.
- **Left planned for 29C.** Records for implementing a driver or device in
  Python: `Driver`'s virtuals, `Device::getName`,
  `OutputDevice::sendMessage` and `addDriver`.
