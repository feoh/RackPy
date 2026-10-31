# Troubleshooting

Rack keeps running when a RackPy plugin fails: the plugin is left out, or a
module falls back to a placeholder, and Rack's log says why. Start there.

## Find the log

Rack writes `log.txt` in its user folder, rewriting it each time it starts.
On Linux that is usually `~/.local/share/Rack2/log.txt`. Every RackPy line
contains `RackPy`:

```bash
grep RackPy ~/.local/share/Rack2/log.txt
```

A plugin that loaded logs, in order, `RackPy interpreter started`, a line
with RackPy's, Python's and Rack's versions, and `Loaded plugin <Slug>`.

While Rack runs, {py:func}`rackpy.runtime.diagnostics` returns what RackPy
recorded instead of interrupting Rack, such as Python errors in callbacks,
refused calls and slow callbacks.

## The plugin does not load

| Log says | Why | What to do |
| --- | --- | --- |
| `RackPy refused to start: a CPython runtime is already loaded in this process` | Another plugin that embeds Python, or a second RackPy plugin, loaded first. One Rack process can have one Python. | Remove the other plugin from `plugins-lin-x64`, or keep only one RackPy plugin. |
| `RackPy refused to start: bundled Python runtime missing or incomplete` | The plugin's `python/` folder is missing, or comes from another RackPy build. | Build the plugin again from a fresh bundle (`bin/rackpy-dev bundle` into an empty folder). |
| `RackPy refused to start: ... rackpy-loader.json: ...` | The loader configuration is invalid; the message names the field. | Rebuild the plugin with `bin/rackpy-plugin build` instead of editing the file. |
| `... resolves outside the plugin folder` | The plugin's host library or `pythonpath` is a link to somewhere else. | Copy the files into the plugin instead of linking them. |
| `RackPy refused to start: cannot load ...` | A library failed to load: usually a development build on another computer, which needs the glibc it was built with. | Build RackPy on the computer that runs Rack. |
| `RackPy host initialization failed (3): entry module '...' (named in rackpy-loader.json) is not in .../pythonpath` | The entry package is missing. | Check `rackpy-plugin.json` names your package, and rebuild. |
| `RackPy host initialization failed (3): entry module '...' imports '...', which is not in the plugin's pythonpath` | Your code imports a package the plugin does not ship. RackPy never uses packages installed on the computer. | Copy the package into the plugin's `pythonpath/`, or remove the import. |
| `RackPy entry module '...' failed:` followed by a traceback | Your entry package raised while Rack loaded it. | Fix the error the traceback shows. |
| `RackPy host initialization failed (4): the entry module's models do not match plugin.json's "modules"` | The models your code adds and the modules `plugin.json` lists differ; the message lists which. Rack rejects such a plugin. | Add the missing `plugin.add_model` calls, or fix `plugin.json` (`bin/rackpy-plugin add-module` keeps both in step). |
| `the plugin's pythonpath has json, ..., named like part of Python's standard library` | A file or folder in `pythonpath/` has the name of a standard module. Python imports the standard one and ignores yours. | Rename yours. |

The plugin loaded but is not in the module browser? Check that `plugin.json`
lists the module, and that Rack's log has `Loaded plugin` for your slug: a
second plugin with the same slug is refused by Rack itself.

## A module shows "(failed)"

Rack asked your plugin for a module and your Python raised, so RackPy gave Rack
a **placeholder**: a module with no controls, on a panel labelled "(failed)".
The log says `RackPy: model <Slug> could not create its module`, with the
traceback before it. The placeholder keeps everything the patch saved for that
module, so saving the patch loses nothing. Fix the error, restart Rack, and
the real module comes back with its saved state
([plugins](how-to/plugins.md#when-a-module-factory-fails)).

If only the panel fails, the log says `could not create its module widget;
using a plain panel`: the module works, on a plain panel.

## Python errors while Rack runs

An error in a callback (a widget's drawing, a menu, an event) is logged as
`RackPy: Python raised in <where>: <error>` with a traceback, and Rack goes on
with its own behaviour for that call. After 20 such messages, RackPy logs at
most one a second, so a callback that fails on every frame cannot fill your
disk; {py:func}`rackpy.runtime.diagnostics` keeps the latest 256.

| Exception | Usually means |
| --- | --- |
| {py:class}`~rackpy.errors.StaleHandleError` | You kept a module, widget or port that Rack has since deleted. Look it up again instead of keeping it. |
| {py:class}`~rackpy.errors.ThreadError` | You called something that needs Rack's main thread from another thread. |
| {py:class}`~rackpy.errors.OwnershipError` | The call is not allowed in this state, such as configuring a module outside `configure`, or changing what Rack's engine owns. The message says which. |
| {py:class}`~rackpy.errors.EngineLockError` | You called something that locks Rack's engine from inside a callback that already holds that lock (a reset or randomize handler). Do it later, from the UI. |
| {py:class}`~rackpy.errors.ContextError` | You used a Rack object outside the Rack window it belongs to. |

## The audio stutters

Python never runs per sample, so it cannot make audio stutter by being slow,
with one exception: Rack stops audio while it runs a module's reset, randomize,
bypass and "load from JSON" handlers, so a slow `on_reset` or `from_json` is a
dropout. RackPy records `slow_exclusive_callback` in
{py:func}`rackpy.runtime.diagnostics` when one takes over 1 ms. Do slow work
later, from the UI, or in a thread of your own.

## Still stuck

Search the [issues](https://github.com/feoh/rackpy/issues), or open one with
your Rack version, the RackPy commit and the `RackPy` lines of the log. Report
security problems privately ([security](explanation/security.md)).
