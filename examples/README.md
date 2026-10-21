# Planned ports of official Rack examples

**No ports are implemented yet.** This inventory defines future work and avoids
pretending that Python-shaped pseudocode is a working Rack plugin.

## Required source set

| Port group | Upstream source | Expected deliverable |
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
77 samples in all, each with a classification, a port task and a content hash.
Check it against the live manual with `bin/manual-sources capture` followed by
`bin/manual-sources check`.

**Gate:** the manual's license is unclear, so every manual-derived port is
`blocked-pending-license` until the owner decides
([ADR 001](../docs/decisions/001-licensing-and-identity.md)). Ports of the
`helper.py` templates are clear under GPL-3.0-or-later.

Do not copy upstream mistakes blindly: verify event signatures against the pinned
headers (the guide's randomize example should not override a ResetEvent).
Record corrections as intentional port changes. The known errata are listed in
[licensing](../docs/licensing.md#errata-found-while-classifying).

## Optional stretch references, not “official samples”

[Fundamental](https://github.com/VCVRack/Fundamental/tree/10dd0160c664770910e5584b7b00498cc48d9ddd)
is a production module collection, not a small sample pack. Selected VCA-1,
LFO or Scope behavior may provide additional tests, but porting the entire
collection is not a substitute for SDK coverage and is not required by this plan.
Its source is GPLv3+; its panel visual designs have separate restrictive terms.
Do not clone its panels, layout, logos or branding.

## Required layout for each completed port

```text
examples/<sample>/
  README.md             # source, purpose, steps, expected result, restrictions
  PROVENANCE.md         # source URL/revision or hash, license, changes
  plugin.json           # version/slug rules verified against actual Rack
  module.py             # actual working facade calls
  native/               # only the native work required for real-time operation
  res/                  # original assets and their licenses
  tests/                # native/Python parity plus actual-host checks
```

A completed port must load in actual Rack, work in module-browser previews where
module can be null, survive save/reopen and deletion, and have a no-audio-device
test path. A pure-Python offline reference is useful evidence, but is not the
production port. Do not claim tutorials are ported until the bindings run them.

Manual snippets/artwork need explicit redistribution provenance before adaptation.
Until then, link to the originals rather than copying them into this repository.
