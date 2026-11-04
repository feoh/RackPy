# Let the user choose a file

Use :mod:`rackpy.dialog` when a plugin needs a user-selected sample, impulse
response, or other file. RackPy wraps the native
file chooser bundled with the pinned Rack SDK; it is separate from the
`rack.hpp` binding inventory. `ModuleWidget.load_dialog()` is for Rack module
presets, not arbitrary files.

The dialogs are modal and wait on Rack's main/UI thread. Call them from a UI
callback, such as a menu action—not from `process`, a Rack engine callback, or a
worker thread. A dialog pauses its caller while the user decides. The selected
path is only a path: RackPy does not open, copy, execute, or deserialize the
file for you. Treat its contents as untrusted input.

```{rackpy-example} file_dialog.py
:caption: Define a file-picker callback for a module's UI
```

When the callback is invoked, handle cancellation before loading anything:

`open_file()` returns a `pathlib.Path` or `None` when cancelled.
`save_file()` returns a proposed destination but does not create or overwrite
it. `choose_directory()` returns a directory path or `None`. Each accepts an
optional starting `directory`; `save_file()` also accepts a default `filename`.
Filters use osdialog's `Name:extension,extension;Other:extension` syntax, for
example `"Audio:wav,aiff;Any:*"`. Omitting filters lets the operating system
show all files.

A picked path outside the patch is not portable with the patch. For state that
must travel with a `.vcv`, copy validated data into
[`module.patch_storage()`](state.md#files-next-to-the-patch) from a UI/lifecycle
callback and persist only a safe identifier or metadata in JSON. Keep file
reading and decoding off the production audio path; this API does not provide a
sample decoder or an audio-file player.
