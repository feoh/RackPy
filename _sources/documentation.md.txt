# Documentation specification

Documentation is a product deliverable, not a dump of generated method names.
The future site should use four complementary kinds of writing.

## Tutorials — a guided first success

1. Install a known-good platform artifact and confirm plugin/runtime versions.
2. Build a sine oscillator, as the official tutorial does, using Python configuration and native DSP.
3. Add a control, a port, a light and an original panel.
4. Save, reopen and safely remove a module.
5. Diagnose a deliberately broken script without crashing Rack.

Each tutorial states prerequisites, duration, supported versions, the expected
screen/signal, complete source, validation commands and troubleshooting.
Do not assume readers know C++, Rack internals, modular synthesis or Python packaging.

## How-to guides — specific jobs

Cover polyphony, menus, custom drawing/text, dark panels, framebuffers, expander
messages, patch storage, plugin settings, audio/MIDI, native-kernel extensions,
buffer views, testing, profiling, packaging and SDK migration. Explain safe
threading and teardown when relevant, not only on a distant warnings page.

## Explanation — mental models

Explain voltage units and 1V/octave, sample versus block processing, signal
flow/feedback, Python/native trade-offs, the callback laboratory, ownership and
context affinity, synchronous serialization, trusted scripts and distribution
licensing. Include small diagrams and concrete failure examples.

## Reference — exact answers

Provide every raw binding and facade entry with signature, return value,
exceptions, defaults, ownership, thread/real-time restrictions, supported
platforms, upstream cross-link and runnable example. Generate the full
C++ → Python map and coverage report from the same manifest used by CI.
Manually authored prose explains intent; generated docs cannot replace it.

## Proposed site structure

```text
index / installation / quickstart
tutorials/oscillator / controls-and-state
how-to/{polyphony,menus,drawing,expanders,audio-midi,native-kernels,packaging}
explanation/{execution,ownership,threading,security,compatibility}
reference/{facade,raw,mapping,coverage}
examples / troubleshooting / contributing / release-notes
```

RP-09 chose Sphinx with MyST and sphinx-autoapi
([ADR 006](decisions/006-documentation-toolchain.md)). Building the docs never
imports the runtime or opens Rack: API pages come from source and stubs, and
snippets that need Rack run in Rack through the host tests.

## Quality gates

- Warnings-as-errors build; no unresolved internal cross-references.
- All published executable snippets are extracted and tested on a shipped build.
- External link checks run with bounded retries and a reviewed exceptions list.
- Distinguish trusted conceptual pseudocode from executable examples visually.
- Images have alt text; diagrams have a text equivalent; no color-only meaning.
- No fabricated screenshots, outputs or benchmark numbers.
- Test the quickstart in a clean profile with no undeclared tools.
- Have a reader unfamiliar with Rack run the first tutorial; record blockers.
- Version documentation with releases and include API/SDK/Python support tables.

RP-09 built the site skeleton and its checks
([ADR 006](decisions/006-documentation-toolchain.md),
[conventions](contributing-docs.md)). RP-37 wrote the manual to this plan: the
[quickstart](quickstart.md), the [oscillator tutorial](tutorials/oscillator.md),
the how-to guides, the [execution](explanation/execution.md) and
[security](explanation/security.md) explanations,
[troubleshooting](troubleshooting.md), and the generated
[C++ to Python crosswalk](reference/crosswalk.md). `tests/docs/test_tutorial.py`
runs the quickstart's and the tutorial's own commands and plays the result in
Rack. Still open: release notes (RP-44), and a tutorial run by a reader new to
Rack, which only a person can do.
