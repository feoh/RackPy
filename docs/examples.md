# Examples

**No examples exist yet.** RackPy will have an original, runnable example for
every feature that the official VCV Rack tutorial, plugin API guide and plugin
template demonstrate; none of them copies the VCV manual
([ADR 001](decisions/001-licensing-and-identity.md)).

The plan, the required layout of each example and the list of features are in
[examples/README.md](../examples/README.md). The 77 official samples, with
their source sections and the RackPy task that covers each, are in
[upstream/examples.json](../upstream/examples.json).

Every code listing on this site that claims to run is a file in
`docs/snippets/` that the test suite runs, in Rack where it needs Rack
([conventions](contributing-docs.md)). Today there is one:

```{rackpy-example} rack_runtime.py
:caption: Which RackPy and Python run inside Rack
```
