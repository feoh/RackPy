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

**RP-29B — audio ports and Rack's audio drivers**
(`tk-rackpy-29b-audio-ports-and-rack-s-audio-drivers-d3e047`). Validation:
[rp-29b-validation.md](rp-29b-validation.md).

- **Drivers and devices.** Rack's audio drivers are `Driver` views by ID.
  Devices are reached through the Python port that holds them, and others'
  ports are `PortView`s, listed under Rack's `processMutex`.
- **Ports.** Python's `Port` is a trampoline (`PyAudioPort`) with
  registry, direct-subscription tracking, context-close release and
  main-thread deletion, as in 29A.
- **Callbacks.** The process callbacks stay native on the audio thread and
  count into `stream_stats`. `on_start_stream` and `on_stop_stream` queue to
  the UI thread (try-lock, bounded, drops counted). Exercising them, and
  Python's synchronous overrides, needs a stream: RP-29C.
- **Test isolation.** Every Rack test run gets `isolated_audio_env`: an
  empty `XDG_RUNTIME_DIR`, Pulse, PipeWire and JACK pointed nowhere, and an
  ALSA config without hardware. Rack's RtAudio drivers load and list no
  devices (the 0-devices case), and RtMidi's ALSA driver fails to start.
  Tests never open the machine's audio or MIDI devices.
- **Found.**
  - `audio::Port::getDeviceDetail` is declared but defined nowhere in Rack
    2.6.6: no source and no libRack symbol. It stays unreviewed pending an
    owner decision on declared-but-undefined API.
  - `audio::Port::getDeviceName` returns `0` as a `std::string` when the
    driver throws. libstdc++ throws `logic_error` for that, which reaches
    Python as an error.

**RP-29C — Python-configured drivers and devices, and the facades**
(`tk-rackpy-29c-python-configured-audio-and-midi-driv-e7a66b`). Every
audio and MIDI record is implemented except `audio::Port::getDeviceDetail`,
which is declared but never defined. Validation:
[rp-29c-validation.md](rp-29c-validation.md).

- **Audio.** `ConfiguredDriver`/`ConfiguredDevice` answer every virtual
  from atomics, and locked fields for names and sets. Rack's
  `setSampleRate`/`setBlockSize` apply the value and record a request
  (`take_requests`). `PythonDevice.start_stream`/`process_buffer`/
  `stop_stream` run Rack's `Device` code on Python's thread with the GIL
  released. A thread-local `pythonDriving` flag makes Python ports'
  overrides run synchronously there, with copies of the port's part of the
  buffers written back. A `DeviceLease` keeps a removed device alive while
  Python drives it.
- **MIDI.** Configured input devices deliver `push_message` through Rack's
  `InputDevice::onMessage`. Configured output devices queue `sendMessage`
  calls from engine threads (try-lock ring, 1024, drops counted) for
  `take_sent`.
- **Hotplug.** A removed device disappears from Rack's lists, stops its
  stream, refuses use, and is deleted when its last port leaves, as with
  RtAudio and RtMidi.
- **The deterministic fixture.** Core's Audio-2 module, subscribed to a
  `PythonDevice` through its JSON, claims the master module and steps Rack's
  engine by the device's blocks. Core's MIDI-CV module subscribes to a
  `PythonInputDevice`.
- **Facades.** `rackpy.audio` provides `drivers`, `find_driver`,
  `open_port`, `virtual_driver` and `Stream`. `rackpy.midi` provides message
  builders, `drivers`, `find_driver`, `open_input`, `open_output` and
  `virtual_driver`.
