# SPDX-License-Identifier: GPL-3.0-or-later
"""The callback laboratory: Python overrides of a module's audio-thread callbacks, run offline.

Rack calls a module's ``process`` once per sample on its engine threads,
where RackPy never runs Python: production modules process audio with native
kernels (:mod:`rackpy.dsp.kernel`). The laboratory runs Python there anyway,
for experiments and tests. A laboratory module (a :class:`ModuleClass`) runs
only on a :class:`Bench`, an offline host that steps RackPy modules from
Python on Rack's main thread the way Rack's engine would. Rack's engine never
runs a laboratory module, a patch cannot create one, and nothing here is
real-time audio. See *Callbacks and the laboratory* in the explanations.

Trusted code must call :func:`enable` first. Available inside Rack only.
"""

import warnings
from typing import Any, Self

try:
    from _rackpy_host import laboratory as _lab
except ImportError as e:
    raise ImportError("rackpy.laboratory is available only inside Rack, in a RackPy plugin") from e

from rackpy import plugin as _plugin
from rackpy.engine import Module
from rackpy.raw.plugin import Model

__all__ = ["Bench", "ModuleClass", "ProcessArgs", "disable", "enable", "enabled"]

Bench = _lab.Bench
ProcessArgs = Module.ProcessArgs

# The Rack virtuals a laboratory module may override, by their Python names.
_OVERRIDES: tuple[str, ...] = _lab.OVERRIDES


def enable() -> None:
    """Allow laboratory modules and benches in this process, until :func:`disable`.

    Call it from trusted code: your plugin's entry module or a test. Loading a
    patch never enables the laboratory. Rack's log records that it was enabled.
    Main thread.
    """
    _lab.enable()


def disable() -> None:
    """Refuse new laboratory modules and benches, and stepping existing benches, again."""
    _lab.disable()


def enabled() -> bool:
    """Whether :func:`enable` was called (and :func:`disable` not since)."""
    return _lab.enabled()


class ModuleClass(_plugin.ModuleClass):
    """A module class whose every ``rack::engine::Module`` callback may be Python, on a laboratory :class:`Bench`.

    ``configure`` and the custom-data handlers ``to_json`` and
    ``from_json`` work as in :class:`rackpy.plugin.ModuleClass`. The
    other methods are true overrides of Rack's virtuals, called synchronously
    where Rack would call them and *instead of* Rack's implementation:
    ``super()`` runs it (Rack's, with RackPy's lifecycle snapshot and data
    handlers), as in a C++ subclass of ``rack::engine::Module``. So an
    :meth:`on_reset` that does not call ``super()`` resets no parameters.
    Rack's deprecated no-argument events (``onAdd()``, ``onReset()``, ...)
    are reached through ``super()`` and need no override of their own.
    Create one with :meth:`create`, never through a model: Rack cannot
    create it.
    """

    _rackpy_laboratory = True

    def __init_subclass__(cls, **kwargs: object) -> None:
        super().__init_subclass__(**kwargs)
        if "step" in vars(cls):
            warnings.warn(
                f"{cls.__qualname__} overrides step(), which Rack deprecates: override process(args) instead",
                DeprecationWarning,
                stacklevel=2,
            )

    def process(self, args: ProcessArgs) -> None:
        """Advance the module by one sample. Rack's implementation calls :meth:`step`."""
        _lab.base_process(self.module, args)

    def process_bypass(self, args: ProcessArgs) -> None:
        """Called instead of :meth:`process` while the bench bypasses the module.

        Rack's implementation copies each bypass route's input to its output.
        """
        _lab.base_process_bypass(self.module, args)

    def step(self) -> None:
        """Deprecated in Rack: override :meth:`process`. Rack's implementation does nothing."""
        warnings.warn("Module.step() is deprecated in Rack: override process(args)", DeprecationWarning, stacklevel=2)
        _lab.base_step(self.module)

    # Rack's module events (AddEvent ... ExpanderChangeEvent), dispatched by a bench in Rack's order.

    def on_add(self, event: Module.AddEvent) -> None:
        """Rack's onAdd: a bench added the module."""
        _lab.base_on_add(self.module, event)

    def on_remove(self, event: Module.RemoveEvent) -> None:
        """Rack's onRemove: a bench is removing the module."""
        _lab.base_on_remove(self.module, event)

    def on_bypass(self, event: Module.BypassEvent) -> None:
        """Rack's onBypass."""
        _lab.base_on_bypass(self.module, event)

    def on_un_bypass(self, event: Module.UnBypassEvent) -> None:
        """Rack's onUnBypass."""
        _lab.base_on_un_bypass(self.module, event)

    def on_port_change(self, event: Module.PortChangeEvent) -> None:
        """Rack's onPortChange: a cable connected or disconnected one of the module's ports."""
        _lab.base_on_port_change(self.module, event)

    def on_reset(self, event: Module.ResetEvent) -> None:
        """Rack's onReset: resets every resettable bounded parameter."""
        _lab.base_on_reset(self.module, event)

    def on_randomize(self, event: Module.RandomizeEvent) -> None:
        """Rack's onRandomize: randomizes every randomizable bounded parameter."""
        _lab.base_on_randomize(self.module, event)

    def on_save(self, event: Module.SaveEvent) -> None:
        """Rack's onSave."""
        _lab.base_on_save(self.module, event)

    def on_sample_rate_change(self, event: Module.SampleRateChangeEvent) -> None:
        """Rack's onSampleRateChange; with an audio device Rack calls it on the audio driver thread."""
        _lab.base_on_sample_rate_change(self.module, event)

    def on_set_master(self, event: Module.SetMasterEvent) -> None:
        """Rack's onSetMaster: the module became the (bench's) master module."""
        _lab.base_on_set_master(self.module, event)

    def on_unset_master(self, event: Module.UnsetMasterEvent) -> None:
        """Rack's onUnsetMaster."""
        _lab.base_on_unset_master(self.module, event)

    def on_expander_change(self, event: Module.ExpanderChangeEvent) -> None:
        """Rack's onExpanderChange: the module on ``event.side`` (0 left, 1 right) changed. Rack's does nothing."""
        _lab.base_on_expander_change(self.module, event)

    # Rack's JSON, synchronous: return JSON-compatible data (None for Rack's NULL).

    def module_to_json(self) -> Any:
        """Rack's ``Module::toJson`` (``Engine::moduleToJson`` calls it); needs a model, see :meth:`create`."""
        return _lab.base_module_to_json(self.module)

    def module_from_json(self, data: Any) -> None:
        """Rack's ``Module::fromJson``: checks the model, then parameters, bypass, expanders and data."""
        _lab.base_module_from_json(self.module, data)

    def params_to_json(self) -> Any:
        """Rack's ``Module::paramsToJson``."""
        return _lab.base_params_to_json(self.module)

    def params_from_json(self, data: Any) -> None:
        """Rack's ``Module::paramsFromJson``."""
        _lab.base_params_from_json(self.module, data)

    def data_to_json(self) -> Any:
        """Rack's ``Module::dataToJson``: here, the ``to_json`` data handler."""
        return _lab.base_data_to_json(self.module)

    def data_from_json(self, data: Any) -> None:
        """Rack's ``Module::dataFromJson``: here, the ``from_json`` data handler."""
        _lab.base_data_from_json(self.module, data)

    @classmethod
    def _build(cls, module: Module) -> Self:
        # Only the data handlers: a laboratory module's events are overrides, not after-the-fact handlers.
        instance = cls(module)
        instance.configure()
        module.set_data_handlers(to_json=instance.to_json, from_json=instance.from_json)
        return instance

    @classmethod
    def create(cls, model: Model | None = None) -> Self:
        """A new laboratory module of this class, configured; put ``instance.module`` on a :class:`Bench`.

        ``model`` only names the module in Rack's JSON (Rack's ``toJson`` and
        ``fromJson`` need one); Rack still never creates or runs it. Raises
        :class:`~rackpy.errors.LaboratoryError` unless the laboratory is enabled. Main thread.
        """
        overrides = [name for name in _OVERRIDES if getattr(cls, name) is not getattr(ModuleClass, name)]
        built: list[Self] = []

        def build(module: Module) -> None:
            instance = cls._build(module)
            _lab.set_overrides(module, **{name: getattr(instance, name) for name in overrides})
            built.append(instance)

        _lab.create_module(build, model)
        return built[0]
