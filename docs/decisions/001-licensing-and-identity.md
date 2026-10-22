# ADR 001: Licensing, sample provenance and identity

- **Status:** Accepted. The sample question was settled by the owner on
  2026-10-21 (option C below).
- **Date:** 2026-10-21
- **Task:** RP-03
- **Evidence:** [licensing](../licensing.md),
  [upstream/examples.json](../../upstream/examples.json)

## Context

RackPy binds the Rack plugin API in-process. The original plan promised
attributed ports of every official tutorial, API guide and template example. Rack itself is
GPL-3.0-or-later. The Rack manual, which holds the tutorial and guide, has no
displayed license and no public source repository. Its only license statement
(CC BY-SA 4.0) sits inside an HTML comment on the manual index. Rack's artwork,
logo and name have their own restrictions.

## Decisions

1. **License.** All original RackPy work is GPL-3.0-or-later (owner decision).
   RackPy does not use Rack's non-commercial plugin exception.
2. **Artwork.** RackPy uses only original panels, fonts it may redistribute, and
   Rack's installed assets loaded at run time. It copies no VCV artwork, logo,
   Core visual design, Component Library SVG or manual image.
3. **Template ports.** Ports of `helper.py` output may proceed under
   GPL-3.0-or-later. Each ported file names the Rack commit and lists its
   changes.
4. **Original examples, not manual ports.** For every official tutorial and API
   guide sample, RackPy writes its own example that demonstrates the same
   feature (`port_gate: original-example` in `upstream/examples.json`) and
   links to the manual section. RackPy does not copy or adapt the manual's
   code, so the manual's unclear license does not apply.
5. **Identity.** The project is RackPy. The proposed Python distribution and
   import name is `rackpy`, and the proposed Rack plugin slug and brand is
   `RackPy`. None of them is registered. Registering them needs owner approval.
   No name uses "VCV". Promotion may say "for VCV Rack".
6. **Releases** carry LICENSE, NOTICE, a generated third-party license manifest
   and a corresponding-source archive ([licensing](../licensing.md#source-offer-and-notices)).

## Options considered for the manual samples

| Option | Outcome |
| --- | --- |
| A. Ask VCV whether CC BY-SA 4.0 applies | Not pursued; no inquiry was sent |
| B. Rely on the commented-out CC BY-SA 4.0 notice | Not pursued |
| **C. Write original examples covering the same features** | **Chosen by the owner** |

The snippets are short API-usage code, and RackPy's examples are Pythonic
rewrites against the bindings, so C loses no teaching coverage. Every sample
keeps its ID, source anchor, content hash and feature in
`upstream/examples.json`, so RP-42 can still prove that each official feature
has a working RackPy example.

## Consequences

- RP-04 onward may proceed: nothing in the binding or embedding work depends on
  the manual's license.
- RP-33 to RP-36 write original examples for every feature in
  `upstream/examples.json`; nothing waits on VCV. RP-33 may also derive the
  plugin template from `helper.py` with attribution.
- `bin/manual-sources check` should pass before an example task starts, so the
  recorded feature list still matches the live manual.
- Any bundled font, Python distribution or binding library needs an entry in
  the licensing record before an artifact ships.
