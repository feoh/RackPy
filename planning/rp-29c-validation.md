# RP-29C validation (Linux x86_64)

Date 2026-10-26, on `main` after e8f74fb. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q tests/io tests/coverage` | 53 passed |
| `uv run pytest -q -m 'not dist and not inventory'` | 212 passed, 31 deselected (626 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `bin/planning-check` | 45 tasks, acyclic |
| `uv run bin/generate_bindings.py check`, `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

In actual Rack's live UI (`tests/io/in_rack_virtual/test_virtual.py`):

- **A Python audio driver.** `add_driver` registers it. -1, a taken ID and a
  second registration are refused. Rack's view of it shows its name, device
  IDs, names and channels.
- **Python ports on a Python device.**
  - `open_port` subscribes a `Port` subclass. Rack's `setDeviceId` stream
    event is queued, not run.
  - `start_stream` runs `on_start_stream` synchronously.
  - `process_buffer` runs the port's `process_input` (with the input
    copy), `process_buffer` and `process_output` in order. The override's
    writes reach the device's output.
  - `stream_stats` counts the call, and short buffers and strides are
    refused.
- **Core's Audio-2 module through a Python device.** It is set up through
  its JSON and is a `PortView` among the subscribers. Its port's rate and
  block-size requests appear in `take_requests`. It claims the master
  module on the first block, and eight 256-frame blocks step the engine by
  at least 2048 frames, with 256-frame blocks.
- **Active-stream module deletion and hotplug.**
  - Removing and dropping Core's module mid-stream takes its port off the
    device, and the stream goes on with Python's port.
  - `remove_device` stops the stream (`on_stop_stream`) and hides the
    device. The removed device refuses to stream.
  - The last port leaving deletes it, and a new device gets the next stable
    ID.
- **A Python MIDI driver.**
  - Pushed notes reach an `InputQueue` in order, with Rack's timestamps.
  - An output's messages, with its channel applied, reach `take_sent`.
  - 1500 sends keep 1024 and count 476 dropped.
  - Core's MIDI-CV module subscribes, and is a `PortView`.
  - Removal hides the device, refuses pushes, and deletes the device when
    the last port leaves.
- **Message helpers.** Bytes, frames, range checks and `LookupError`.
- **The documentation's snippet** (`docs/snippets/virtual_audio.py`): a
  virtual device streams a ramp, and the port's peaks are
  `[0, 0.25, 0.5, 0.75]`.

Windowed Rack (`tests/io/test_midi.py`, extended): an audio `Port`
subclass's `on_start_stream` and `on_stop_stream`, called by Rack's
`setDeviceId` on a Python device, run at RackPy's UI step on the main thread,
not when Rack calls them.

No physical audio or MIDI hardware was used: Rack's RtAudio and RtMidi
drivers are isolated in tests and list no devices. Only Linux x86_64 was
tested.
