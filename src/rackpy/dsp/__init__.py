# SPDX-License-Identifier: GPL-3.0-or-later
"""DSP for RackPy modules. Available inside Rack only.

:mod:`rackpy.dsp.kernel` has the native kernels that run a module's audio
path: Python chooses and configures them, and never runs per sample.
:mod:`rackpy.dsp.basic` has Rack's DSP helpers (conversions, windows,
polynomials, ODE steps) and :mod:`rackpy.dsp.digital` its triggers and
timers, for design, analysis and tests.
"""
