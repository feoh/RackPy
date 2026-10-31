"""RackPy Polyphony: a RackPy plugin. Rack imports this package while it loads the plugin.

RackPy has already loaded kernels/polyphony.so, the plugin's kernel library, by then.
"""

from rackpy.plugin import plugin

from . import polysine

# Models: bin/rackpy-plugin add-module adds each module's import above and its model below.
plugin.add_model("PolySine", polysine.simd, widget=polysine.PolySineWidget)
plugin.add_model("PolySineScalar", polysine.scalar, widget=polysine.PolySineWidget)
