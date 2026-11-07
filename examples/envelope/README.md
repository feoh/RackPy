# Envelope

An original RackPy attack/decay envelope, independently implemented with a
native per-sample kernel. It demonstrates a common envelope-generator workflow
identified while researching Amplified Futures' permissively licensed plugin;
it does not copy that plugin's code, name, panel, or artwork.

The trigger starts a one-pole exponential attack toward 10 V, then a one-pole
decay toward 0 V. ATTACK and DECAY sweep from 1 ms to 2 s. The LOOP switch
restarts the attack after each decay; EOC emits a 1 ms pulse at the end of the
decay. INVERTED is `10 V - ENVELOPE`. The kernel handles every sample natively;
Python configures Rack's module and panel only.

## Build and run

Linux x86_64, uv, CMake, Ninja, GCC and Rack Free 2.6.6. From the repository
root:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
bin/rackpy-plugin build examples/envelope --bundle /tmp/rackpy-bundle/RackPy \
  --kernels build/dev/examples/envelope/kernels --dest ~/.local/share/Rack2/plugins-lin-x64
```

Remove other RackPy plugins first: RackPy currently loads one plugin bundle per
Rack process. Add a trigger source to TRIGGER, then monitor ENVELOPE, INVERTED
or EOC. The attack and decay controls use exponential time scaling; this is a
teaching example, not a calibrated envelope instrument.

## Tests and limitations

The Python reference is a frame-at-a-time teaching model, not an audio callback.
Tests exercise trigger edges, attack/decay endpoints, inversion, EOC and loop
retriggering; actual-Rack tests check the loaded model and patch save/reopen.
The C++ kernel is the production signal path. Linux x86_64 / Rack Free 2.6.6
is the only validated target. The code runs trusted in-process; it is not a
sandbox. See [provenance](PROVENANCE.md).
