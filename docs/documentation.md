# Documentation specification

Documentation is a product deliverable, not a dump of generated method names.
The future site should use four complementary kinds of writing.

## Tutorials — a guided first success

1. Install a known-good platform artifact and confirm plugin/runtime versions.
2. Build the official sine oscillator port using Python configuration and native DSP.
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

RP-09 will choose and pin Sphinx or MkDocs after checking native docstring,
type-stub and cross-reference support. Runtime imports must not require opening
Rack just to read docs: use verified stubs/manifest plus selected in-host doctests.

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

Only this planning documentation exists today. The runtime manual and completed
sample walkthroughs are explicitly future tasks.
