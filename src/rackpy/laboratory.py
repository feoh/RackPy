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

The laboratory's audio and MIDI drivers and devices (:class:`AudioDriver`,
:class:`MidiDriver`, ...) do the same for the driver virtuals that Rack calls on
its audio and MIDI driver threads: Rack never lists them, so only ports you
make and connect reach them, on the main thread.

Trusted code must call :func:`enable` first. Available inside Rack only.
"""

from __future__ import annotations

import functools
import warnings
from typing import TYPE_CHECKING, Any, Self, cast

try:
    from _rackpy_host import laboratory as _lab
except ImportError as e:
    raise ImportError("rackpy.laboratory is available only inside Rack, in a RackPy plugin") from e

from rackpy import plugin as _plugin
from rackpy import raw as _raw
from rackpy.engine import Module
from rackpy.raw import dsp as _dsp
from rackpy.raw import engine as _engine
from rackpy.raw import midi as _midi
from rackpy.raw.plugin import Model

__all__ = [
    "AudioDevice",
    "AudioDriver",
    "Bench",
    "MidiDriver",
    "MidiGenerator",
    "MidiInput",
    "MidiInputDevice",
    "MidiInputQueue",
    "MidiOutputDevice",
    "ModuleClass",
    "ParamQuantity",
    "ProcessArgs",
    "Quantity",
    "SwitchQuantity",
    "disable",
    "enable",
    "enabled",
]

Bench = _lab.Bench
ProcessArgs = Module.ProcessArgs
MidiOutputDevice = _lab.MidiOutputDevice
"""A MIDI output device whose ``send_message`` and ``get_name`` a subclass overrides; ``device.connect(output)``
points a ``rackpy.raw.midi.Output`` you made at it, so Rack's ``Output::sendMessage`` reaches the override. A
:class:`MidiDriver` can also hold it (``add_output_device``)."""

AudioDriver = _lab.AudioDriver
"""``rack::audio::Driver`` whose every virtual a subclass may override, offline.

``driver.connect(port)`` points a ``rackpy.raw.audio.Port`` you made at it,
repeating what Rack's ``Port::setDriverId`` does for a driver Rack lists (Rack
never lists a laboratory driver): the port leaves its device, takes this
driver, then subscribes to ``get_default_device_id()``'s device. Rack's own
port methods then call the driver's ``get_name``, ``get_device_ids``,
``get_device_name``, ``get_device_num_inputs``, ``get_device_num_outputs``,
``subscribe`` and ``unsubscribe`` synchronously, and an override's error
reaches the caller of the port method. ``super()`` answers from the devices
given to ``add_device``, which the driver keeps; ``subscribe`` must return
one of them or ``None``. Main thread; creating one needs :func:`enable`.
Its native part is destroyed on the main thread only: dropped on another
thread, at the next UI step.
"""

AudioDevice = _lab.AudioDevice
"""``rack::audio::Device`` whose every virtual a subclass may override, offline.

Rack's port methods (``get_sample_rate``, ``set_block_size``, ``to_json``,
...) call the device of a port connected to an :class:`AudioDriver`
synchronously; ``super()`` answers with the configured values (``name``,
``num_inputs``, ``sample_rate``, ...), as a ``rackpy.raw.audio.PythonDevice``
does. Creating one needs :func:`enable`.
"""

MidiDriver = _lab.MidiDriver
"""``rack::midi::Driver`` whose every virtual a subclass may override, offline.

``driver.connect(port)`` points a ``rackpy.raw.midi`` port you made at it, as
Rack's ``Port::setDriverId`` does for a driver it lists. Rack's own port
methods (``set_device_id``, ``get_device_ids``, ``get_default_device_id``,
``get_device_name``) and Rack's MIDI widgets then call the driver's virtuals
synchronously. ``super()`` answers from the :class:`MidiInputDevice` and
:class:`MidiOutputDevice` objects added, which the driver keeps. Main thread;
creating one needs :func:`enable`. Its native part is destroyed on the main
thread only: dropped on another thread, at the next UI step.
"""

MidiInputDevice = _lab.MidiInputDevice
"""``rack::midi::InputDevice`` whose ``get_name`` a subclass may override; ``push_message`` is Rack's
``InputDevice::onMessage``, which calls each subscribed input's ``on_message``. Creating one needs :func:`enable`."""

MidiInputQueue = _lab.MidiInputQueue
"""``rack::midi::InputQueue`` whose ``on_message`` a subclass may override, offline.

Rack's ``InputDevice::onMessage`` calls it synchronously on the main thread
(a :class:`MidiInputDevice`'s ``push_message``, Rack's Loopback driver
when Python sends); ``super().on_message(message)`` queues the message for
``try_pop``, as Rack's does. On Rack's threads Python never runs and Rack's
queue takes the message. It is a ``rackpy.raw.midi.Input``, but not a
``rackpy.raw.midi.InputQueue``, which is final. Creating one needs
:func:`enable` and Rack's live context. As for :class:`MidiInput`, no MIDI
subscription may change while ``on_message`` runs.
"""


class MidiInput(_midi.Input):
    """``rack::midi::Input`` whose ``on_message`` runs synchronously, offline.

    An ordinary ``rackpy.raw.midi.Input`` gets its messages queued, on the UI
    thread. A laboratory input's ``on_message`` runs where Rack's
    ``InputDevice::onMessage`` calls it on the main thread (a
    :class:`MidiInputDevice`'s ``push_message``, Rack's Loopback driver when
    Python sends), and its error reaches that caller. On Rack's MIDI driver threads
    Python never runs: the message is queued, as for an ordinary input. A
    native mark set by this constructor, after it checks :func:`enable`,
    gates the synchronous path. Needs Rack's live context.

    Rack is iterating the device's subscribers while ``on_message`` runs, so
    until the call that delivered the message returns, changing any MIDI
    subscription (``set_device_id``, ``connect``, removing a device, making a
    port, ...) raises :class:`~rackpy.errors.LaboratoryError`, and objects
    dropped meanwhile are kept alive until then.
    """

    _rackpy_laboratory = True

    def __init__(self) -> None:
        super().__init__()
        _lab.mark_midi_input(self)

    def on_message(self, message: _midi.Message) -> None:
        """Rack's ``Input::onMessage``: does nothing. A subclass receives each message here, an owned copy."""
        super().on_message(message)


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
    run through Rack's default event methods, but cannot yet be independently
    overridden in Python. Those legacy virtuals remain planned (RP-31F).
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


# Quantities whose value and range methods (get_value, set_value, get_min_value, get_max_value,
# get_default_value) may be Python. Rack also calls these on its engine thread for a module's
# quantities, so outside the laboratory Python cannot override them.


class Quantity(_raw.Quantity):
    """``rack::Quantity`` whose value and range methods a subclass may override, offline.

    Rack's own code that calls them (display values, ``reset``, ``randomize``,
    scaled values, the display string) reaches the override synchronously.
    Creating one needs :func:`enable`. Main thread.
    """

    _rackpy_laboratory = True

    def __init__(self) -> None:
        super().__init__()
        _lab.mark_quantity(self)


class ParamQuantity(_engine.ParamQuantity):
    """``rack::engine::ParamQuantity`` whose value and range methods a subclass may override, offline.

    Only a laboratory module takes one as ``config_param(..., quantity_type=...)``
    or in its ``param_quantities``: Rack's engine can call an ordinary
    module's quantities on its own threads. Rack's code calling them
    (``paramsToJson``, reset, randomize, display) reaches the override
    synchronously. Creating one needs :func:`enable`.
    """

    _rackpy_laboratory = True

    def __init__(self) -> None:
        super().__init__()
        _lab.mark_quantity(self)


class SwitchQuantity(_engine.SwitchQuantity):
    """``rack::engine::SwitchQuantity`` with laboratory value methods; see :class:`ParamQuantity`."""

    _rackpy_laboratory = True

    def __init__(self) -> None:
        super().__init__()
        _lab.mark_quantity(self)


if TYPE_CHECKING:

    class _MidiGenerator(_dsp.MidiGenerator_16):
        """The laboratory specialization also exposes Rack's override point."""

        def on_message(self, message: _midi.Message) -> None: ...


@functools.cache
def _midi_generator(channels: int) -> type[_MidiGenerator]:
    base: type[_dsp.MidiGenerator_16] = _dsp.MidiGenerator[channels]

    def __init__(self: _dsp.MidiGenerator_16) -> None:
        base.__init__(self)
        base._enable_laboratory(self)

    def on_message(self: _dsp.MidiGenerator_16, message: _midi.Message) -> None:
        """Rack's onMessage, called with each message the generator makes: RackPy's records it for take_messages()."""
        base._on_message(self, message)

    doc = (
        f"rack::dsp::MidiGenerator<{channels}> whose on_message a subclass may override, offline: Rack's generator "
        "calls it synchronously with each message it generates, and super().on_message(message) records it for "
        "take_messages(), as RackPy's native implementation does. Creating one needs enable()."
    )
    namespace = {
        "_rackpy_laboratory": True,
        "__init__": __init__,
        "on_message": on_message,
        "__doc__": doc,
        "__module__": __name__,
    }
    return cast("type[_MidiGenerator]", type(f"MidiGenerator_{channels}", (base,), namespace))


class _MidiGeneratorTemplate:
    """``MidiGenerator[N]``, N from 1 to 16: the laboratory class to subclass for a Python ``on_message``."""

    def __getitem__(self, channels: int) -> type[_MidiGenerator]:
        _dsp.MidiGenerator[channels]  # Rack's range, and its error
        return _midi_generator(channels)

    def __repr__(self) -> str:
        return "rackpy.laboratory.MidiGenerator"


MidiGenerator = _MidiGeneratorTemplate()
"""``MidiGenerator[N]``: Rack's ``dsp::MidiGenerator<N>`` whose ``on_message`` a subclass overrides, offline only.

Rack calls ``MidiGenerator::onMessage`` where a module generates MIDI, on its
engine thread, so outside the laboratory RackPy's generator only records the
messages (``take_messages()``).
"""
