# RP-18D validation (Linux x86_64)

Date 2026-10-26, on `main` after af2548a, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

RP-18D (`tk-rackpy-18d-vu-meters-and-the-dsp-midi-generator--3ba44c`) binds
the 115 records of `dsp/vumeter.hpp` and `dsp/midi.hpp`; 114 are
implemented and `MidiGenerator::onMessage` is planned for RP-22's
laboratory. Reference page: [docs/reference/dsp.md](../docs/reference/dsp.md#vu-meters).

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --no-test` | builds |
| `uv run pytest -q tests/coverage tests/dsp tests/simd tests/docs` | 76 passed: the in-Rack DSP suite has 30 tests, 5 of them RP-18D's; the introspection check finds every RP-18D name in the built package |
| `uv run pytest -q tests/dsp/test_basic.py tests/coverage/test_introspection.py` | 4 passed, after the last lint fix |
| `prek run --all-files`, `uv run bin/generate_bindings.py check`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

**Inside actual Rack** (`tests/dsp/in_rack_basic/test_midi.py`, production
build):

- **Reference scripts.** `native/tests/dsp_reference.cpp` runs op scripts
  through Rack's objects and records the result after every op: the
  deprecated `VuMeter` at nine levels (with infinity and NaN) and `VuMeter2`
  in both modes over a 64-frame stream with a reset; `MidiGenerator` with 16,
  4 and 1 channels through every setter, repeated values, the transport,
  `set_channels`, `panic` and `reset`, recording each generated message's
  bytes and frame; and `MidiParser` with 16 channels in every mono mode and
  in rotate, reuse, reset and MPE poly modes, with 4 channels and with 1, over
  28 messages (notes on and off, velocity-0 note on, key and channel
  pressure, pitch wheel, mod wheel, sustain, all notes off, song position,
  clocks, start, continue, stop), each followed by a filter and pulse step,
  recording notes, gates, velocities, aftertouches, wheels, held notes, the
  rotate index, clock, pedal, pitch voltages, wheels and pulses, then Rack's
  `toJson`; and `fromJson` of a full object with the pre-2.5.3 `lastPitch`
  key. Every script replays identically.
- **Behaviour.** Deprecations, the meters' modes, templates and refused
  arguments, the generator's index and range refusals, `take_messages`, the
  `on_message` refusal, the parser's refusals (channel indices, channel
  counts, an MPE message on channel 9 of a 4-channel parser, a clock with
  division 0, three kinds of bad JSON), live array, filter and pulse views,
  and `held_notes` copies.

Only Linux x86_64 was tested.
