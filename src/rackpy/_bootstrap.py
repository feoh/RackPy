# SPDX-License-Identifier: GPL-3.0-or-later
"""Start-up and shutdown hooks that the RackPy host calls inside Rack. Internal; not API.

The host imports this module on Rack's main thread during plugin ``init()``,
with the interpreter already configured in isolated mode, and calls
:func:`start`. It calls :func:`stop` from plugin ``destroy()`` just before
finalizing the interpreter.
"""

import importlib
import sys
import traceback

from rackpy import __version__

try:
    import _rackpy_host
except ImportError as e:
    raise ImportError(
        "rackpy._bootstrap runs only inside the RackPy host in Rack; the _rackpy_host module is missing"
    ) from e

# The host ABI this package was written for. It must equal the host's RACKPY_HOST_ABI.
HOST_ABI = 2


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
        _import_entry(plugin_dir, entry)


def _import_entry(plugin_dir: str, entry: str) -> None:
    """Import the trusted entry module; log the traceback and name what is missing if that fails."""
    try:
        importlib.import_module(entry)
    except BaseException as error:
        _rackpy_host.log_warning(f"RackPy entry module {entry!r} failed:\n{traceback.format_exc().rstrip()}")
        if isinstance(error, ModuleNotFoundError) and error.name:
            if entry == error.name or entry.startswith(error.name + "."):
                raise ImportError(
                    f"entry module {entry!r} (named in rackpy-loader.json) is not in {plugin_dir}/pythonpath"
                ) from error
            raise ImportError(
                f"entry module {entry!r} imports {error.name!r}, which is not in the plugin's pythonpath: "
                "RackPy uses only the plugin's own files and Python's standard library"
            ) from error
        raise


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
