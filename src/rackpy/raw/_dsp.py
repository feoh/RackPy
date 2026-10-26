# SPDX-License-Identifier: GPL-3.0-or-later
"""rack::dsp::Frame<CHANNELS, T>, as a Python class template (RP-18A).

Rack's ``Frame`` is plain data, ``T samples[CHANNELS]``, which its ring buffers
and sample-rate converter cast to ``float*``. It has no code to call, so it is
written here, for any ``CHANNELS`` and for ``T = float`` (the default) or
``simd.float_4``: ``Frame[16]`` is ``Frame<16>``, ``Frame[4, float_4]`` is
``Frame<4, simd::float_4>``. The samples are float32 storage that the frame
exposes through the buffer protocol in Rack's memory layout.

This module adds ``Frame`` to the native ``rackpy.raw.dsp``, Rack's
deprecated ``VUMeter`` typedef, which warns when used, and the refusal of a
``MidiGenerator`` subclass that defines ``on_message``.
"""

import array
import builtins
import warnings
from collections.abc import Iterator
from typing import Any, ClassVar

import _rackpy_host

from rackpy.raw.simd import float_4

_MODULE = "rackpy.raw.dsp"


class Float4Samples:
    """A ``Frame<CHANNELS, float_4>``'s samples: indexing reads a vector out and writes one in."""

    def __init__(self, storage: array.array[float]) -> None:
        self._storage = storage

    def __len__(self) -> int:
        return len(self._storage) // 4

    def _offset(self, i: int) -> int:
        n = len(self)
        if i < 0:
            i += n
        if not 0 <= i < n:
            raise IndexError(f"sample index out of range (the frame has {n} samples)")
        return 4 * i

    def __getitem__(self, i: int) -> float_4:
        k = self._offset(i)
        return float_4.load(memoryview(self._storage)[k : k + 4])

    def __setitem__(self, i: int, value: float_4 | float) -> None:
        k = self._offset(i)
        float_4(value).store(memoryview(self._storage)[k : k + 4])

    def __iter__(self) -> Iterator[float_4]:
        for i in range(len(self)):
            yield self[i]

    def __repr__(self) -> str:
        return f"Float4Samples({list(self)!r})"


class Frame:
    """``rack::dsp::Frame<CHANNELS, T>``: ``CHANNELS`` samples of ``T``. Subscript it: ``Frame[16]``.

    A specialization is a mutable value. Its samples start at zero (C++ leaves
    them uninitialized). ``samples`` is a live view; ``memoryview(frame)`` is
    the float32 storage in Rack's layout (``CHANNELS * 4`` floats for
    ``float_4``, lane by lane).
    """

    CHANNELS: ClassVar[int]
    type: ClassVar[builtins.type]
    _specializations: ClassVar[dict[tuple[int, builtins.type], builtins.type[Frame]]] = {}
    _storage: array.array[float]

    def __class_getitem__(cls, args: Any) -> builtins.type[Frame]:
        given = args if isinstance(args, tuple) else (args,)
        if cls is not Frame:
            raise TypeError(f"{cls.__qualname__} is already a specialization")
        if not 1 <= len(given) <= 2:
            raise TypeError("Frame takes CHANNELS and an optional sample type: Frame[16] or Frame[4, float_4]")
        channels, sample_type = (*given, float)[:2]
        if isinstance(channels, bool) or not isinstance(channels, int):
            raise TypeError(f"Frame's CHANNELS is an int, not {channels!r}")
        if channels < 1:
            raise ValueError(f"Frame needs at least one channel (C++ has no zero-length arrays), not {channels}")
        if sample_type not in (float, float_4):
            raise TypeError(f"Frame's sample type is float or float_4, not {sample_type!r}")
        key = (channels, sample_type)
        spec = cls._specializations.get(key)
        if spec is None:
            name = f"Frame[{channels}, {sample_type.__name__}]"
            attrs = {"__module__": _MODULE, "__qualname__": name, "CHANNELS": channels, "type": sample_type}
            spec = builtins.type(name, (cls,), attrs)
            cls._specializations[key] = spec
        return spec

    def __init__(self) -> None:
        if builtins.type(self) is Frame:
            raise TypeError(
                "Frame is a class template: instantiate a specialization, Frame[CHANNELS] or Frame[CHANNELS, T]"
            )
        lanes = 4 if self.type is float_4 else 1
        self._storage = array.array("f", bytes(4 * lanes * self.CHANNELS))

    @property
    def samples(self) -> memoryview | Float4Samples:
        """``T samples[CHANNELS]``: a writable memoryview for float, a vector view for float_4."""
        return Float4Samples(self._storage) if self.type is float_4 else memoryview(self._storage)

    def __buffer__(self, flags: int, /) -> memoryview:
        return memoryview(self._storage)

    def __copy__(self) -> Frame:
        other = builtins.type(self)()
        other._storage[:] = self._storage
        return other

    def __deepcopy__(self, memo: dict[int, Any]) -> Frame:
        return self.__copy__()

    __hash__ = None  # type: ignore[assignment]  # a mutable value

    def __repr__(self) -> str:
        values: list[Any] = list(Float4Samples(self._storage)) if self.type is float_4 else self._storage.tolist()
        return f"{builtins.type(self).__qualname__}({values!r})"


Frame.__module__ = _MODULE
Float4Samples.__module__ = _MODULE
_rackpy_host.raw.dsp.Frame = Frame
_rackpy_host.raw.dsp.Float4Samples = Float4Samples


def _key_ring_buffers_by_frame() -> None:
    """Key the ring buffers' Frame instantiations by Frame[C], written above after the native module loaded (RP-19C)."""
    for specializations in _rackpy_host.raw.dsp._ringbuffers.SPECIALIZATIONS.values():
        for key in [k for k in specializations if isinstance(k[0], tuple)]:
            specializations[(Frame.__class_getitem__(key[0][1]), *key[1:])] = specializations.pop(key)


_key_ring_buffers_by_frame()


def _deprecated_alias(name: str) -> Any:
    if name == "VUMeter":
        warnings.warn(
            "rackpy.raw.dsp.VUMeter mirrors Rack's deprecated typedef of VuMeter, itself deprecated; use VuMeter2",
            DeprecationWarning,
            stacklevel=2,
        )
        return _rackpy_host.raw.dsp.VuMeter
    raise AttributeError(f"module 'rackpy.raw.dsp' has no attribute {name!r}")


def _refuse_on_message(cls: builtins.type, /, **kwargs: Any) -> None:
    if getattr(cls, "_rackpy_laboratory", False):
        return  # rackpy.laboratory.MidiGenerator[N]: offline only
    if any("on_message" in vars(base) for base in cls.__mro__):
        raise TypeError(
            f"{cls.__qualname__} defines on_message: Rack calls MidiGenerator::onMessage where a module generates "
            "MIDI, on its engine thread, where Python never runs (ADR 003), so a Python override would never be "
            "called. RackPy's generator records the messages instead: call take_messages() (or, offline only, "
            "subclass rackpy.laboratory.MidiGenerator[N])."
        )


for _generator in _rackpy_host.raw.dsp.MidiGenerator.specializations.values():
    _generator.__init_subclass__ = classmethod(_refuse_on_message)

_rackpy_host.raw.dsp.__getattr__ = _deprecated_alias
_rackpy_host.raw.dsp.__all__ = [*sorted(n for n in vars(_rackpy_host.raw.dsp) if not n.startswith("_")), "VUMeter"]
