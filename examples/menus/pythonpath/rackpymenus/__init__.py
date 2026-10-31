"""RackPy Menus: a RackPy plugin. Rack imports this package while it loads the plugin."""

from rackpy.plugin import plugin

from . import switchboard

# Models: bin/rackpy-plugin add-module adds each module's import above and its model below.
plugin.add_model("Switchboard", switchboard.Switchboard, widget=switchboard.SwitchboardWidget)
