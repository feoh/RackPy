"""RackPy Expanders: a RackPy plugin. Rack imports this package while it loads the plugin."""

from rackpy.plugin import plugin

from . import peeker, receiver, source

# Models: bin/rackpy-plugin add-module adds each module's import above and its model below.
plugin.add_model("Source", source.configure, widget=source.SourceWidget)
plugin.add_model("Receiver", receiver.configure, widget=receiver.ReceiverWidget)
plugin.add_model("Peeker", peeker.configure, widget=peeker.PeekerWidget)
