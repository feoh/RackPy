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
from typing import Self

try:
    from _rackpy_host import laboratory as _lab
except ImportError as e:
    raise ImportError("rackpy.laboratory is available only inside Rack, in a RackPy plugin") from e

from rackpy import plugin as _plugin
from rackpy.engine import Module

__all__ = ["Bench", "ModuleClass", "ProcessArgs", "disable", "enable", "enabled"]

Bench = _lab.Bench
ProcessArgs = Module.ProcessArgs

# The overrides a laboratory module may define, in addition to rackpy.plugin.ModuleClass's.
_OVERRIDES = ("process", "process_bypass", "step")


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
    """A module class whose ``process`` and ``process_bypass`` may be Python, on a laboratory :class:`Bench`.

    Everything :class:`rackpy.plugin.ModuleClass` offers works the same way.
    In addition you may override :meth:`process`, :meth:`process_bypass`
    and the deprecated :meth:`step`; ``super()`` reaches Rack's own
    implementations, as in a C++ subclass of ``rack::engine::Module``. Create
    one with :meth:`create`, never through a model: Rack cannot create it.
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

    @classmethod
    def create(cls) -> Self:
        """A new laboratory module of this class, configured; put ``instance.module`` on a :class:`Bench`.

        Raises :class:`~rackpy.errors.LaboratoryError` unless the laboratory is enabled. Main thread.
        """
        overrides = [name for name in _OVERRIDES if getattr(cls, name) is not getattr(ModuleClass, name)]
        built: list[Self] = []

        def build(module: Module) -> None:
            instance = cls._build(module)
            _lab.set_overrides(module, **{name: getattr(instance, name) for name in overrides})
            built.append(instance)

        _lab.create_module(build)
        return built[0]
