# RP-29B validation (Linux x86_64)

Date 2026-10-26, on `main` after 8b3728b. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q tests/io` | 3 passed: MIDI and audio live-UI suites, MIDI windowed session |
| `uv run pytest -q -m 'not dist and not inventory'` | 211 passed, 31 deselected (588 s), with every Rack run audio-isolated |
| `uv run pytest -q -m inventory` | 29 passed |
| `bin/planning-check` | 45 tasks, acyclic |
| `uv run bin/generate_bindings.py check`, `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

In actual Rack's live UI (`tests/io/in_rack_audio/test_audio.py`), isolated
from this machine's sound servers and devices:

- **Drivers.** RtAudio's ALSA, PulseAudio and JACK load and list no
  devices: the 0-devices case. Their device queries return empty values,
  and an unknown driver ID gives `None`.
- **A port without a device.**
  - It takes Rack's first driver (ALSA) and no device.
  - Its device queries are empty, and rate and block setters do nothing.
  - A device the driver cannot open leaves no device.
  - An unknown driver falls back to the first, and JACK can be chosen by
    ID.
  - `stream_stats` is all zeros.
- **Offsets and JSON.** Offsets and channel maxima round-trip through
  `to_json`/`from_json`, and `reset` returns to the first driver.
- **Callbacks.** Rack's base callbacks are callable no-ops.
- **Errors.** `Driver.subscribe` to a missing device raises Rack's
  `Exception` ("device"), and the port keeps no device.
- **Shutdown.** A port kept alive at Rack's quit is released before Rack
  destroys the drivers, and Rack exits.

Not covered here: real streams, Python's stream callbacks and Python-made
drivers and devices (RP-29C). No physical audio device was used. Only Linux
x86_64 was tested.
