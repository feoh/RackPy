"""RackPy Oscillator: a RackPy plugin. Rack imports this package while it loads the plugin."""

from rackpy.plugin import plugin

from . import oscillator

# Models: bin/rackpy-plugin add-module adds each module's import above and its model below.
plugin.add_model("Oscillator", oscillator.configure, widget=oscillator.OscillatorWidget)
