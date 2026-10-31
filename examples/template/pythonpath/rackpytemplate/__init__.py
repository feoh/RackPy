"""RackPy Template: a RackPy plugin. Rack imports this package while it loads the plugin."""

from rackpy.plugin import plugin

from . import starter

# Models: bin/rackpy-plugin add-module adds each module's import above and its model below.
plugin.add_model("Starter", starter.configure, widget=starter.StarterWidget)
