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
from rackpy.raw.app import ModuleWidget
from rackpy.raw.plugin import Model

__all__ = ["Model", "ModuleClass", "Plugin", "on_destroy", "plugin"]

_EVENTS = ("on_add", "on_remove", "on_bypass", "on_un_bypass", "on_port_change", "on_reset", "on_randomize", "on_save")


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

    # Rack's module events. Override the ones you need: only overridden ones are
    # installed, and each runs on Rack's main thread after Rack's own behaviour.
    # Rack's audio stops while on_add, on_remove, on_bypass, on_un_bypass,
    # on_port_change, on_reset and on_randomize run: keep them under 1 ms.

    def on_add(self, event: Any) -> None:
        """Rack's engine added the module."""

    def on_remove(self, event: Any) -> None:
        """Rack's engine is removing the module; it still exists."""

    def on_bypass(self, event: Any) -> None:
        """The module was bypassed."""

    def on_un_bypass(self, event: Any) -> None:
        """The module stopped being bypassed."""

    def on_port_change(self, event: Any) -> None:
        """A cable was connected to or disconnected from one of the module's ports (``event.connecting``,
        ``event.type``, ``event.port_id``)."""

    def on_reset(self, event: Any) -> None:
        """Initialize: Rack has already reset the parameters."""

    def on_randomize(self, event: Any) -> None:
        """Randomize: Rack has already randomized the parameters."""

    def on_save(self, event: Any) -> None:
        """The patch is being saved: write patch-storage files here."""

    @classmethod
    def factory(cls) -> Callable[[Module], None]:
        """A ``Module.create`` constructor callback that builds the module through this class."""
        events = [name for name in _EVENTS if getattr(cls, name) is not getattr(ModuleClass, name)]

        def build(module: Module) -> None:
            instance = cls(module)
            instance.configure()
            # The native module owns these bound methods, and through them the instance.
            module.set_data_handlers(to_json=instance.to_json, from_json=instance.from_json)
            if events:
                module.set_event_handlers(**{name: getattr(instance, name) for name in events})

        build.__qualname__ = f"{cls.__qualname__}.factory.<locals>.build"
        return build


class Plugin:
    """The RackPy plugin that is loading your entry module; C++ plugins call it ``pluginInstance``."""

    @property
    def raw(self) -> _raw.Plugin:
        """Rack's plugin object (:class:`rackpy.raw.plugin.Plugin`)."""
        return _raw.plugin_instance()

    def add_model(
        self,
        slug: str,
        module: Callable[[Module], object] | type[ModuleClass],
        *,
        widget: Callable[[Module | None], ModuleWidget] | None = None,
        hp: int = 4,
    ) -> Model:
        """Add a model. Rack builds one of its modules, and its widget, whenever it needs one.

        ``module`` is either ``Module.create``'s constructor callback,
        ``module(m)``, or a :class:`ModuleClass` subclass. ``widget`` is a
        ``rackpy.raw.app.ModuleWidget`` subclass, or a function, that Rack
        calls with the module (``None`` for a module browser preview); the
        widget calls ``self.set_module(module)`` first, as Rack's do. Without
        ``widget`` the module gets a plain panel ``hp`` wide. Call this only
        from the entry module while Rack loads the plugin.
        """
        if isinstance(module, type):
            if not issubclass(module, ModuleClass):
                raise TypeError(f"a module class must subclass rackpy.plugin.ModuleClass, not {module.__name__}")
            module = module.factory()
        model = _raw.create_model(slug, module, widget_factory=widget, hp=hp)
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
