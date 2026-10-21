# ADR 001: Licensing, sample provenance and identity

- **Status:** Accepted for licensing and identity. **Open** for manual-derived
  sample ports, which wait for the owner.
- **Date:** 2026-10-21
- **Task:** RP-03
- **Evidence:** [licensing](../licensing.md),
  [upstream/examples.json](../../upstream/examples.json)

## Context

RackPy binds the Rack plugin API in-process and plans attributed ports of every
official tutorial, API guide and template example. Rack itself is
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
4. **Manual-derived ports are blocked** (`blocked-pending-license` in
   `upstream/examples.json`) until the owner picks an option below. Original
   bindings, API documentation and original examples are **not** blocked.
5. **Identity.** The project is RackPy. The proposed Python distribution and
   import name is `rackpy`, and the proposed Rack plugin slug and brand is
   `RackPy`. None of them is registered. Registering them needs owner approval.
   No name uses "VCV". Promotion may say "for VCV Rack".
6. **Releases** carry LICENSE, NOTICE, a generated third-party license manifest
   and a corresponding-source archive ([licensing](../licensing.md#source-offer-and-notices)).

## Options for the manual-derived ports

| Option | What happens | Trade-off |
| --- | --- | --- |
| **A. Ask VCV** (recommended) | Send the draft below. If VCV confirms CC BY-SA 4.0, or grants permission, ports proceed with attribution. | Needs your approval to send. Waiting does not block binding work. |
| B. Rely on the hidden notice | Treat the commented-out CC BY-SA 4.0 statement as the license. License adapted files as GPLv3, which Creative Commons lists as a one-way compatible license. | Risk rests on an unpublished statement. Adapted files would be GPLv3 rather than "or later". |
| C. Write equivalents | Write original examples for each sample's feature, without adapting the manual's code. Link to the manual section. | No permission needed, but these are not "attributed ports" as the project description says. That change needs your sign-off. |

Recommendation: **A**, falling back to **C** if VCV does not answer before
RP-33 starts. RP-33 to RP-36 keep the gate until you decide.

### Draft inquiry (not sent)

> **To:** support@vcvrack.com
> **Subject:** License for code samples in the VCV Rack manual
>
> Hello,
>
> I maintain RackPy (https://github.com/feoh/RackPy), an independent,
> GPL-3.0-or-later project that provides Python bindings for the Rack 2 plugin
> API. We would like to include Python ports of the code samples in the Plugin
> Development Tutorial and the Plugin API Guide, credited to VCV and linked to
> the original sections.
>
> The manual pages do not display a license. The source of
> https://vcvrack.com/manual/ contains a commented-out note saying the manual
> text is licensed under CC BY-SA 4.0. Could you confirm whether that license
> applies to the manual, including its code samples? If it does not, may we
> adapt the samples in a GPL-3.0-or-later project with attribution?
>
> We will not use VCV artwork, logos or the VCV name in the project.
>
> Thank you,
> Christopher Patti

## Consequences

- RP-04 onward may proceed: nothing in the binding or embedding work depends on
  the manual's license.
- RP-33 can port the `helper.py` templates now and must leave the tutorial's
  `process()` port behind the gate.
- `bin/manual-sources check` must pass before a port task starts, so that the
  recorded hashes still describe the live manual.
- Any bundled font, Python distribution or binding library needs an entry in
  the licensing record before an artifact ships.
