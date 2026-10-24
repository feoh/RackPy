# SPDX-License-Identifier: GPL-3.0-or-later
"""Add this plugin's models to Rack. Available inside Rack only.

A RackPy plugin's trusted entry module adds its models while Rack loads the
plugin, and ``plugin.json``'s ``"modules"`` must list exactly the same slugs.
A model's modules come from a function or from a :class:`ModuleClass`. See
the how-to page *Register models*. The raw layer is :mod:`rackpy.raw.plugin`.
"""

from collections.abc import Callable
from typing import Any

from rackpy.engine import Module
from rackpy.raw import plugin as _raw
from rackpy.raw.plugin import Model

__all__ = ["Model", "ModuleClass", "Plugin", "on_destroy", "plugin"]


class ModuleClass:
    """A model's module written as a class: one instance per module Rack creates.

    Override :meth:`configure` to set the module up, as ``Module.create``'s
    constructor callback would, and :meth:`to_json` and :meth:`from_json` for
    its custom state. The instance lives as long as Rack's module, and
    :attr:`module` is that module.
    """

    module: Module
    """The module this instance belongs to."""

    def __init__(self, module: Module) -> None:
        self.module = module

    def configure(self) -> None:
        """Configure :attr:`module`: ``config()``, ``config_param()`` and the rest. Runs once, first."""

    def to_json(self) -> Any:
        """The module's custom data when Rack saves it; ``None``, the default, saves none."""
        return None

    def from_json(self, data: Any) -> None:
        """Restore the custom data Rack loaded. Validate it: it comes from a patch file."""

    @classmethod
    def factory(cls) -> Callable[[Module], None]:
        """A ``Module.create`` constructor callback that builds the module through this class."""

        def build(module: Module) -> None:
            instance = cls(module)
            instance.configure()
            # The native module owns these bound methods, and through them the instance.
            module.set_data_handlers(to_json=instance.to_json, from_json=instance.from_json)

        build.__qualname__ = f"{cls.__qualname__}.factory.<locals>.build"
        return build


class Plugin:
    """The RackPy plugin that is loading your entry module; C++ plugins call it ``pluginInstance``."""

    @property
    def raw(self) -> _raw.Plugin:
        """Rack's plugin object (:class:`rackpy.raw.plugin.Plugin`)."""
        return _raw.plugin_instance()

    def add_model(self, slug: str, module: Callable[[Module], object] | type[ModuleClass], *, hp: int = 4) -> Model:
        """Add a model. Rack builds one of its modules whenever it needs one.

        ``module`` is either ``Module.create``'s constructor callback,
        ``module(m)``, or a :class:`ModuleClass` subclass. Call this only from
        the entry module while Rack loads the plugin. Until Python module
        widgets exist, the module gets a plain panel ``hp`` wide.
        """
        if isinstance(module, type):
            if not issubclass(module, ModuleClass):
                raise TypeError(f"a module class must subclass rackpy.plugin.ModuleClass, not {module.__name__}")
            module = module.factory()
        model = _raw.create_model(slug, module, hp=hp)
        self.raw.add_model(model)
        return model

    def set_settings_handlers(
        self,
        *,
        to_json: Callable[[], Any] | None = None,
        from_json: Callable[[Any], object] | None = None,
    ) -> None:
        """Save plugin-wide settings in Rack's ``settings.json``, under this plugin's slug.

        Rack calls ``to_json()`` whenever it saves its settings, including as
        it exits; returning ``None`` keeps what is stored. It calls
        ``from_json(data)`` once, right after the plugin initializes, if
        anything is stored, so set the handlers from the entry module. Errors
        are logged and contained. Validate ``data``: users can edit the file.
        """
        _raw.set_settings_handlers(to_json=to_json, from_json=from_json)


def on_destroy(callback: Callable[[], None]) -> Callable[[], None]:
    """Run ``callback`` when Rack calls the plugin's ``destroy()``, as it exits.

    That is after :func:`rackpy.runtime.on_context_closing`: every handle to a
    Rack object is stale by then, so use it for Python-side cleanup only.
    Callbacks run on the main thread, most recently registered first; one
    that raises is logged and the others still run. Returns ``callback``, so
    it works as a decorator.
    """
    return _raw.on_destroy(callback)


plugin = Plugin()
"""This plugin."""
