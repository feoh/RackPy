# Feedback Governor

An original mono feedback-loop utility: SEND is summed with a controlled RETURN;
the return is low-pass filtered, DC-blocked, scaled by FEEDBACK × PER-PASS DECAY
and optional 0–10 V CV, and the final output is hard-limited by OUTPUT CAP.
The KILL button or gate silences output and clears filter history. Cable OUTPUT
through a delay or other effect back to RETURN to make an external loop. The
kernel runs once per Rack sample with preallocated scalar state and no Python.

The implementation uses RackPy's public plugin facade for module configuration
and its native-kernel API for audio processing. It is deliberately mono; it does
not claim polyphonic loop processing. CV adds up to +1.0 to the feedback amount,
then clamps it to [0, 1]. The output cap bounds the emitted voltage, not the
voltage of an external feedback source. See [PROVENANCE](PROVENANCE.md) for the
behavioral reference and independence statement.

## Build and try

The repository-wide CMake build discovers this native kernel automatically.
Build and stage it with the standard developer tools:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
bin/rackpy-plugin build examples/feedback-governor --bundle /tmp/rackpy-bundle/RackPy \\
  --kernels build/dev/examples/feedback-governor/kernels --dest ~/.local/share/Rack2/plugins-lin-x64
```

Linux x86_64 / Rack Free 2.6.6 only. The host tests load the plugin and compare
its kernel output with the independent reference; a physical external cable
loop and performance measurements are not claimed.
