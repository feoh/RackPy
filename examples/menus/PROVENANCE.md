# Provenance: menus

## Sources covered

| Sample | Feature | RackPy equivalent |
| --- | --- | --- |
| `guide-12` | Context menu: separator, label, item, bool pointer, submenu, index submenu | `SwitchboardWidget.append_context_menu`, which also uses a check item, a bool item, a disabled item and an index pointer submenu |

The content hash is recorded with the sample in `upstream/examples.json`
(`bin/manual-sources check` confirmed on 2026-10-31 that the manual's blocks
are unchanged). Its disposition there names these files and their tests.

## How it was made

The project and the first `switchboard.py` were generated with
`bin/rackpy-plugin create` and `add-module` from `res/Switchboard.svg` (see
[the template](../template/PROVENANCE.md) for that tool's provenance), then
rewritten as a `ModuleClass` with a context menu.

This is an original example demonstrating the guide's feature, written from
the pinned Rack headers and RackPy's API. No VCV manual code or text is copied
or adapted ([ADR 001](../../docs/decisions/001-licensing-and-identity.md),
option C).

## Original assets

`res/Switchboard.svg` is original RackPy artwork (GPL-3.0-or-later), not
derived from VCV's or Fundamental's panels. The jack, light and screws are
Rack's component library, drawn from Rack's own resources at runtime.
