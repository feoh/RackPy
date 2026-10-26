# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's services, Pythonically: files and archives (:mod:`rackpy.services.system`) and HTTP
(:mod:`rackpy.services.network`), over :mod:`rackpy.raw.system` and :mod:`rackpy.raw.network`.
Inside Rack only.
"""

from rackpy.services import network, system

__all__ = ["network", "system"]
