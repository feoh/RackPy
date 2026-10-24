# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-16A test entry module: models that differ from plugin.json's "modules"."""

from rackpy.plugin import plugin

plugin.add_model("PyAlpha", lambda m: m.config(0, 0, 0))
plugin.add_model("PyExtra", lambda m: m.config(0, 0, 0))
