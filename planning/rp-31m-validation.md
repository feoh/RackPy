# RP-31M investigation — owner review required

Rack 2.6.6 `src/plugin/Plugin.cpp:14-20` shows `Plugin::~Plugin()` clears
`model->plugin` for each model and does not delete the models. RackPy exposes
loaded plugins as borrowed `PluginHandle` objects only; `Model.plugin` is
observable, but a Python caller cannot destroy the plugin. The existing
`on_destroy` test covers a different C hook, not this destructor.

One possible mapping is an isolated, Python-owned plugin with explicit close,
which calls the real destructor while retaining its models until their
back-pointers have been cleared. Model handles need identity, lifetime and
thread checks, including when models outlive the plugin or are dropped before
it. This must not register the isolated plugin in Rack or change loaded
plugins. It is not yet implemented or tested.

**Open owner review item:** whether to permit attachment of a detached model to
such an isolated plugin. The existing RP-16 contract and
`docs/reference/plugin.md#plugins` restrict `Plugin.add_model` to this Rack
plugin during initialization; this proposal would add an exception. Do not
silently change that owner-approved rule or count a native-only fixture as
Python coverage. The manifest remains `planned` and carries the owner-review
marker; RP-31D remains blocked by RP-31M. The owner may instead choose a
specific contract that preserves complete supported semantics.

This is an investigation, not validation of an implementation. No host or
platform support was established by this document.
