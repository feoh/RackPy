# Examples covering the official Rack samples

Implemented (RP-33 to RP-35), Linux x86_64 in Rack Free 2.6.6:

- [template](template/README.md): the plugin-template workflow with
  `bin/rackpy-plugin` (create, add a module from a panel, build).
- [oscillator](oscillator/README.md): the tutorial's sine oscillator and blink
  as a native kernel, with an offline reference and a C++ comparison.
- [configuration](configuration/README.md): every parameter display, a
  labelled switch, a button, descriptions, port and light names, bypass
  routes, a custom bypass and factory presets, compared in Rack with the same
  modules written in C++.
- [state-and-events](state-and-events/README.md): a sequencer's JSON state,
  patch-storage file and plugin settings through Rack's save and reopen, its
  Initialize and Randomize handlers, and a laboratory script for every event.
- [menus](menus/README.md): one of each of Rack's context-menu helpers, with
  browser previews and menus that outlive their module.
- [dark-panels](dark-panels/README.md): a light and a dark panel, with themed
  screws and jack, and a lamp that stays lit in a dark room.
- [drawing](drawing/README.md): NanoVG shapes, text, a self-lit layer and a
  cached framebuffer, checked in Rack's real frames and shown in the
  [walkthrough](../docs/how-to/draw-panels.md).

The rest of this page defines the remaining work (RP-36). It avoids
pretending that Python-shaped pseudocode is a working Rack plugin.

RackPy writes **original** examples. Each one demonstrates a feature that the
official Plugin Development Tutorial, Plugin API Guide or plugin template
demonstrates, and links to that section. RackPy does not copy or adapt the
manual's code or text ([ADR 001](../docs/decisions/001-licensing-and-identity.md)).

## Required source set

| Example group | Feature source | Expected deliverable |
| --- | --- | --- |
| Tutorial oscillator | [Development tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial) | Python configuration/UI, native sine/blink kernel, Python offline reference and rendered-signal parity tests |
| Module configuration | [API guide: configuration](https://vcvrack.com/manual/PluginGuide#Module-configuration) | Param ranges/display scaling, labels, switch/button and bypass examples |
| Lifecycle/state | [API guide](https://vcvrack.com/manual/PluginGuide) | Reset/randomize/sample-rate events, JSON, patch storage, plugin settings |
| Menus | [API guide: context menu](https://vcvrack.com/manual/PluginGuide#Context-menu) | Actions, bools, indexed submenu, browser-preview handling |
| Expanders | [API guide: expanders](https://vcvrack.com/manual/PluginGuide#Expanders) | Adjacent-module and double-buffer messaging, one-sample-latency tests |
| Panels/drawing | [API guide](https://vcvrack.com/manual/PluginGuide) | Original light/dark panels, shapes, text, self-illuminating layer, framebuffer caching |
| Polyphony/SIMD/DSP | [API guide](https://vcvrack.com/manual/PluginGuide) | Polyphonic behavior and every guide snippet, including native vector processing |
| Generated SDK template | [Pinned helper.py](https://github.com/VCVRack/Rack/blob/061ccf63c1758599396ac1bb10d47345d9d34076/helper.py) | Equivalent plugin/model/manifest scaffold and runnable module |

The exhaustive inventory is [upstream/examples.json](../upstream/examples.json)
(RP-03). It assigns every code block on the tutorial, API guide, panel, presets,
migration and building pages a stable ID, and adds the `helper.py` templates:
77 samples in all, each with a classification, an example task and a content
hash. Every sample marked `original-example` needs a RackPy example covering its
feature; RP-42 checks that none is missing. Check the list against the live
manual with `bin/manual-sources capture` followed by `bin/manual-sources check`.

The `helper.py` templates are GPL Rack source, so RP-33 may derive RackPy's
plugin template from them with attribution.

Write examples against the pinned headers, not the manual's wording: several
samples have errors (the guide's randomize example overrides a `ResetEvent`).
The known errata are listed in
[licensing](../docs/licensing.md#errata-found-while-classifying).

## Optional stretch references, not “official samples”

[Fundamental](https://github.com/VCVRack/Fundamental/tree/10dd0160c664770910e5584b7b00498cc48d9ddd)
is a production module collection, not a small sample pack. Selected VCA-1,
LFO or Scope behavior may provide additional tests, but porting the entire
collection is not a substitute for SDK coverage and is not required by this plan.
Its source is GPLv3+; its panel visual designs have separate restrictive terms.
Do not clone its panels, layout, logos or branding.

## Required layout for each completed example

Each example is a plugin project as `bin/rackpy-plugin create` makes it, so
`bin/rackpy-plugin build` turns it into a plugin Rack loads:

```text
examples/<sample>/
  README.md             # feature, purpose, exact commands, expected result, restrictions
  PROVENANCE.md         # sample IDs covered, manual sections linked, any derived GPL source
  plugin.json           # the manifest, with its own slug
  rackpy-plugin.json    # the entry package's name
  pythonpath/           # the entry package and modules: actual working facade calls
  res/                  # original assets and their licenses
  presets/              # optional factory presets, presets/<module slug>/*.vcvm
tests/examples/         # native/Python parity plus actual-host checks
```

Native code is added only where real-time operation needs it. Each sample the
example covers gets a `disposition` in `upstream/examples.json` naming its
files and tests (`tests/examples/test_dispositions.py`).

A completed example must load in actual Rack, work in module-browser previews
where module can be null, survive save/reopen and deletion, and have a
no-audio-device test path. A pure-Python offline reference is useful evidence,
but is not the production example. Do not claim a feature is covered until the
bindings run its example.

Never copy manual code, text or artwork into this repository; link to it.
