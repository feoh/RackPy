# RP-29A validation (Linux x86_64)

Date 2026-10-26, on `main` after 574d52c. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q tests/io` | 2 passed |
| `bin/planning-check` | 45 tasks, acyclic |
| `uv run pytest -q -m 'not dist and not inventory'` | first run: 204 passed, 5 failed and 1 error. The causes were the unresolved callback strategies (resolved in `upstream/callback-rules.json` and ADR 003) and a full 31 GB `/tmp`: each hooks bundle and staged plugin copied about 400 MB. Since then, bundle copies hard-link files of 1 MiB or more (`tests/support.copy_sharing_binaries`); then 210 passed, 31 deselected (612 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `uv run bin/callback-matrix check --write` | 434 virtual methods, 0 unresolved (42 driver) |
| `uv run bin/generate_bindings.py check`, `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

In actual Rack's live UI (`tests/io/in_rack/test_midi.py`), with Rack's
Loopback MIDI driver (-12):

- **Messages.** Rack's byte rules: masking, missing bytes, sizes, frames
  and the hex string. Byte and `uint8_t` ranges are checked.
- **Drivers.** Loopback's 16 devices and their names and defaults.
  Computer keyboard/mouse and Gamepad are present, and this machine also
  has RtMidi's ALSA and JACK. An unknown ID gives `None`.
- **Through the loopback.**
  - An `Output` reaches an `InputQueue` with its channel applied.
  - A future-frame message waits for its frame.
  - The input's channel filter drops other channels.
  - `InputQueue.on_message` queues.
- **JSON and devices.**
  - `to_json`/`from_json` round-trip by device name.
  - Device views report their names and subscribers.
  - A driver subscription to device 2 receives, and is refused a second
    unsubscribe; its view goes stale.
  - `InputDevice.on_message` simulates input, and `OutputDevice.send_message`
    sends without a channel.
  - A released device's view goes stale.
- **Context and abstraction.** `Port` is abstract.
- **Port virtuals.** A Python `get_default_device_id` override is called by
  Rack's `set_driver_id`.
- **Other subscribers.** Core's MIDI-CV module, set to the loopback through
  its JSON, is a `PortView` among the device's subscribers. It goes stale
  when the module moves to another device.
- **The documentation's snippet** (`docs/snippets/midi_loopback.py`) opens
  a loopback pair. Its note is not delivered on the sending thread.

Windowed Rack (`tests/io/test_midi.py`):

- Making a port at plugin start-up raises `ContextError`.
- A Python `Input` subclass gets nothing while the sender runs. At RackPy's
  next UI steps, `on_message` receives the five notes in order, on the main
  thread only.
- 2000 messages in one frame: 1024 are delivered, and 976 are dropped and
  counted in `dropped`.
- Ports still alive when Rack quits are released while the drivers exist,
  and Rack exits cleanly.

Only Linux x86_64 was tested. No physical MIDI hardware was used: the
RtMidi drivers are listed but not exercised.
