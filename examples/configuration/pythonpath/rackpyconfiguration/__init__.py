"""RackPy Configuration: a RackPy plugin. Rack imports this package while it loads the plugin."""

from rackpy.plugin import plugin

from . import shaper, splitter

# Models: bin/rackpy-plugin add-module adds each module's import above and its model below.
plugin.add_model("Shaper", shaper.configure, widget=shaper.ShaperWidget)
plugin.add_model("Splitter", splitter.configure, widget=splitter.SplitterWidget)
