# SPDX-License-Identifier: GPL-3.0-or-later
"""Start-up and shutdown hooks that the RackPy host calls inside Rack. Internal; not API.

The host imports this module on Rack's main thread during plugin ``init()``,
with the interpreter already configured in isolated mode, and calls
:func:`start`. It calls :func:`stop` from plugin ``destroy()`` just before
finalizing the interpreter.
"""

import importlib
import sys

from rackpy import __version__

try:
    import _rackpy_host
except ImportError as e:
    raise ImportError(
        "rackpy._bootstrap runs only inside the RackPy host in Rack; the _rackpy_host module is missing"
    ) from e

# The host ABI this package was written for. It must equal the host's RACKPY_HOST_ABI.
HOST_ABI = 1


def start(plugin_dir: str, entry: str) -> None:
    """Check the host, log the runtime facts, then import the trusted entry module, if any."""
    if _rackpy_host.HOST_ABI != HOST_ABI:
        raise RuntimeError(
            f"rackpy {__version__} needs host ABI {HOST_ABI}, but the host provides {_rackpy_host.HOST_ABI}"
        )
    _rackpy_host.log_info(
        f"rackpy {__version__}, Python {sys.version.split()[0]}, {_rackpy_host.BINDING}, "
        f"Rack {_rackpy_host.rack_version()}, plugin {plugin_dir}"
    )
    if entry:
        importlib.import_module(entry)


def context_closing() -> None:
    """Rack is shutting down and the bound context is still alive: let plugin code stop its threads."""
    from rackpy import runtime

    runtime._run_closing_callbacks()


def stop() -> None:
    """Called from plugin destroy(), before the interpreter is finalized.

    If RackPy never bound a context (no RackPy module was ever created),
    context_closing() did not run, so the closing callbacks run here instead.
    """
    from rackpy import runtime

    runtime._run_closing_callbacks()
