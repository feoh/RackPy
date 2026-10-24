# SPDX-License-Identifier: GPL-3.0-or-later
"""Add this plugin's models to Rack. Available inside Rack only.

A RackPy plugin's trusted entry module adds its models while Rack loads the
plugin, and ``plugin.json``'s ``"modules"`` must list exactly the same slugs.
See the how-to page *Register models*. The raw layer is :mod:`rackpy.raw.plugin`.
"""

from collections.abc import Callable

from rackpy.engine import Module
from rackpy.raw import plugin as _raw
from rackpy.raw.plugin import Model

__all__ = ["Model", "Plugin", "plugin"]


class Plugin:
    """The RackPy plugin that is loading your entry module; C++ plugins call it ``pluginInstance``."""

    @property
    def raw(self) -> _raw.Plugin:
        """Rack's plugin object (:class:`rackpy.raw.plugin.Plugin`)."""
        return _raw.plugin_instance()

    def add_model(self, slug: str, module: Callable[[Module], object], *, hp: int = 4) -> Model:
        """Add a model. Rack calls ``module(m)`` whenever it creates one of its modules.

        ``module`` is ``Module.create``'s constructor callback: configure
        the module there. Call this only from the entry module while Rack
        loads the plugin. Until Python module widgets exist, the module gets a
        plain panel ``hp`` wide.
        """
        model = _raw.create_model(slug, module, hp=hp)
        self.raw.add_model(model)
        return model


plugin = Plugin()
"""This plugin."""
