# SPDX-License-Identifier: GPL-3.0-or-later
"""Native kernels: a module's production audio path. Available inside Rack only.

Rack calls a module's ``process()`` once per sample, so the code that runs
there is native. Python picks a kernel type, wires it to the module's ports,
parameters and lights by index, and hands it over with
``module.set_kernel(...)``; afterwards ``Kernel.set`` changes its controls
through a bounded queue. See the how-to page *Process audio*.
"""

try:
    from _rackpy_host import dsp as _dsp
except ImportError as e:
    raise ImportError("rackpy.dsp.kernel is available only inside Rack, in a RackPy plugin") from e

__all__ = ["CONTROL_CAPACITY", "Gain", "Kernel", "KernelType", "Sine", "kernel_types", "load"]

Kernel = _dsp.Kernel
KernelType = _dsp.KernelType
CONTROL_CAPACITY: int = _dsp.CONTROL_CAPACITY
"""How many control values may wait for the engine; ``Kernel.set`` raises ``ValueError`` beyond it."""

Gain: KernelType = _dsp.kernel_type("rackpy.Gain")
"""``Gain(input=, output=, param=-1, gain=1.0)``: each channel times ``gain`` (a control), and the param if wired."""

Sine: KernelType = _dsp.kernel_type("rackpy.Sine")
"""``Sine(output=, pitch_input=-1, pitch_param=-1, amplitude=5.0)``: a V/oct sine per pitch channel (0 V is C4)."""


def kernel_types() -> tuple[KernelType, ...]:
    """Every kernel type RackPy knows, built in or loaded."""
    return _dsp.kernel_types()


def load(name: str) -> KernelType:
    """The kernel type named ``"namespace.Name"``, e.g. ``"rackpy.Gain"``; ``KeyError`` if there is none."""
    return _dsp.kernel_type(name)
