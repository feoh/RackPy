"""RackPy State and Events: a RackPy plugin. Rack imports this package while it loads the plugin."""

from rackpy.plugin import plugin

from . import sequencer

# Plugin-wide settings: Rack calls load right after this import, if settings.json has any.
plugin.set_settings_handlers(to_json=sequencer.save_settings, from_json=sequencer.load_settings)

# Models: bin/rackpy-plugin add-module adds each module's import above and its model below.
plugin.add_model("Sequencer", sequencer.Sequencer, widget=sequencer.SequencerWidget)
