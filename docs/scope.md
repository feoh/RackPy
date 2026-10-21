# Scope and upstream research

## Target and evidence

Research baseline: **Rack v2.6.6**, commit
`061ccf63c1758599396ac1bb10d47345d9d34076`.
These sources were read during planning; this is not a binary-compatibility test.
RP-02 kept this baseline and pinned the SDK archives; see
[compatibility](compatibility.md). The reached headers and how they map to the
families below are in [coverage](coverage.md#pinned-surface-rp-02).

| Source | What it establishes |
| --- | --- |
| [Pinned rack.hpp](https://github.com/VCVRack/Rack/blob/061ccf63c1758599396ac1bb10d47345d9d34076/include/rack.hpp) | Entry point and public-header list; non-rack.hpp includes are unsupported; PRIVATE declarations must not be called |
| [Pinned Module.hpp](https://github.com/VCVRack/Rack/blob/061ccf63c1758599396ac1bb10d47345d9d34076/include/engine/Module.hpp) | Per-sample process, configuration, JSON, bypass, events, polyphony-related ports and expander ownership |
| [Pinned context.hpp](https://github.com/VCVRack/Rack/blob/061ccf63c1758599396ac1bb10d47345d9d34076/include/context.hpp) | Context is set per thread; APP is a macro for contextGet() |
| [Pinned plugin callbacks](https://github.com/VCVRack/Rack/blob/061ccf63c1758599396ac1bb10d47345d9d34076/include/plugin/callbacks.hpp) | C ABI init/destroy and plugin-settings hooks |
| [Development tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial) | Official sine oscillator, manifest, panel, model registration and build workflow |
| [API guide](https://vcvrack.com/manual/PluginGuide) | Serialization, menus, expanders, drawing, polyphony, SIMD and DSP examples |
| [Rack API reference](https://vcvrack.com/docs-v2/) | Useful navigation; not a substitute for a pinned source inventory |
| [Rack license](https://github.com/VCVRack/Rack/blob/061ccf63c1758599396ac1bb10d47345d9d34076/LICENSE.md) | GPLv3+, plugin exception, separate artwork terms and trademark restrictions |
| [Plugin licensing](https://vcvrack.com/manual/PluginLicensing) | GPLv3+ plugin option and distribution guidance |

The manual website is mutable. RP-02/RP-03 must record the retrieved content
hash and redistribution permissions before copying or adapting its snippets.
A public VCVRack/manual repository was not found during research; do not invent
a pinned source revision for it.

## “The entire API” means…

The supported public **plugin** API of a pinned Rack SDK, not Rack Pro's DAW-host
API, every third-party plugin's private implementation, or undocumented internals.

Inventory all publicly accessible Rack declarations reachable through rack.hpp,
including transitive headers, overloads, public members, events, constants,
platform conditionals, relevant macros, and templates. A header's presence alone
does not prove all its fields are public API: internal pointers and PRIVATE
declarations need explicit classification.

Also inventory external types required by that interface: Jansson JSON, NanoVG
drawing data, window/graphics handles, MIDI/audio buffers, and shared resources.
Expose the necessary semantics, not every unrelated dependency API.

Never silently redefine complete to mean “all symbols we happened to bind”.
Python has no direct equivalent of arbitrary C++ template instantiation or a
preprocessor. Record faithful factories, typed specializations, or extension
mechanisms and their limits. If useful public behavior cannot be represented,
escalate the scope decision and keep the full-coverage release blocked.

## Coverage families

1. Common/value utilities: math, color, strings, random, logger, quantity, mutexes,
   weak pointers and the common macros.
2. Engine: module lifecycle/configuration, params, ports, lights, engine, cables,
   parameter handles, events, bypass, state and expanders.
3. Plugin/model registration, callbacks, asset lookup and helpers.
4. Widget tree, layout/transforms/framebuffers, UI controls/menus, Rack app widgets,
   SVG resources, component library and input events.
5. Window/rendering, required NanoVG operations, resources and context.
6. DSP: all publicly reachable dsp headers and templates; SIMD vectors/functions.
7. Audio/MIDI driver/device/port interfaces and callbacks.
8. Settings, system/filesystem, network, synchronization and other public
   transitive families discovered by the inventory.
9. Public context-dependent history/patch operations, if supported by the
   rack.hpp closure. Merely forward-declared internals do not become supported
   API by guessing their definitions. RP-02 found that `history.hpp` is in the
   closure (through `app/ModuleWidget.hpp`), so its public actions are in scope.
   `patch::Manager` is only forward-declared, so `Context::patch` is an opaque
   handle.

## Not established yet

No claim is made that embedding CPython is distributable on every target, that
every virtual callback is safe on the main thread, that the public engine can
drive an offline callback laboratory, or that Rack Pro will accept this plugin.
These are explicit feasibility tasks. The baseline is not a promise that 2.6.6
will remain the latest release.
