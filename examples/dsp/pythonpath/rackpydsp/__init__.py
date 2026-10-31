"""RackPy DSP: a RackPy plugin. Rack imports this package while it loads the plugin."""

from rackpy.plugin import plugin

from . import filter

# Models: bin/rackpy-plugin add-module adds each module's import above and its model below.
plugin.add_model("Filter", filter.configure, widget=filter.FilterWidget)
