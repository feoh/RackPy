# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-16A test entry module: adds a model, then fails while Rack loads the plugin."""

from rackpy.plugin import plugin

plugin.add_model("PyAlpha", lambda m: m.config(0, 0, 0))
raise RuntimeError("rp16-entry-intentional-error")
