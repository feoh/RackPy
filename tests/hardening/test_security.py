# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38: what RackPy runs, and what it never runs, in actual Rack (the production bundle, headless).

RackPy runs the plugin's own trusted Python, and nothing else: not code a Python
environment variable points at, not files named like the standard library, not .pth
or sitecustomize files, and never anything a patch carries. Every "evil" file here
would leave a marker file if Python ever ran it; no marker may appear.
"""

import base64
import json
import os
import pickle
from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest

from tests.support import assert_clean_exit, linux_x64_only, run_rack, stage_plugin, write_patch

pytestmark = [pytest.mark.host, linux_x64_only()]


def evil(marker: Path, name: str) -> str:
    """Python that leaves `marker`/`name` behind if anything ever runs it."""
    return f"open({str(marker / name)!r}, 'w').close()\n"


def pth(marker: Path, name: str) -> str:
    return "import os; " + evil(marker, name)


# The trusted entry module: records what the interpreter looks like, then imports what tests ask for.
PROBE = """
import json, sys
from pathlib import Path

CONFIG = json.loads((Path(__file__).resolve().parent / "probe_config.json").read_text())
facts = {
    "path": sys.path,
    "isolated": sys.flags.isolated,
    "ignore_environment": sys.flags.ignore_environment,
    "safe_path": sys.flags.safe_path,
    "no_site": sys.flags.no_site,
    "no_user_site": sys.flags.no_user_site,
    "dont_write_bytecode": sys.dont_write_bytecode,
    "pycache_prefix": sys.pycache_prefix,
    "site_loaded": "site" in sys.modules,
    "warnings": list(sys.warnoptions),
}
for name in CONFIG.get("imports", []):
    module = __import__(name)
    facts[name] = getattr(module, "__file__", None)
import rackpy
facts["rackpy"] = rackpy.__file__
Path(CONFIG["result"]).write_text(json.dumps(facts))
"""


def stage(make_bundle: Callable[[], Path], user: Path, result: Path, files: dict[str, str], **config: Any) -> Path:
    probe_config = json.dumps({"result": str(result), **config})
    return stage_plugin(
        make_bundle(),
        user,
        entry="security_probe",
        files={"pythonpath/security_probe.py": PROBE, "pythonpath/probe_config.json": probe_config, **files},
    )


def no_markers(marker: Path) -> None:
    assert sorted(p.name for p in marker.iterdir()) == [], "Python ran a file it must never run"


def test_python_environment_variables_are_ignored(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    marker, bad = tmp_path / "markers", tmp_path / "evil"
    marker.mkdir()
    user_site = bad / "userbase" / "lib" / "python3.14" / "site-packages"
    user_site.mkdir(parents=True)
    for rel, text in {
        "json.py": evil(marker, "PYTHONPATH-json"),
        "rackpy/__init__.py": evil(marker, "PYTHONPATH-rackpy"),
        "sitecustomize.py": evil(marker, "sitecustomize"),
        "usercustomize.py": evil(marker, "usercustomize"),
        "startup.py": evil(marker, "PYTHONSTARTUP"),
        "lib/python3.14/os.py": evil(marker, "PYTHONHOME-os"),
    }.items():
        (bad / rel).parent.mkdir(parents=True, exist_ok=True)
        (bad / rel).write_text(text)
    (user_site / "evil.pth").write_text(pth(marker, "user-site-pth"))
    result = tmp_path / "facts.json"
    stage(make_bundle, tmp_path, result, {}, imports=["json"])
    write_patch(tmp_path, [])
    env = {
        "PYTHONPATH": str(bad),
        "PYTHONHOME": str(bad),
        "PYTHONSTARTUP": str(bad / "startup.py"),
        "PYTHONUSERBASE": str(bad / "userbase"),
        "PYTHONINSPECT": "1",
        "PYTHONSAFEPATH": "",
        "PYTHONWARNINGS": "error",
        "PYTHONPYCACHEPREFIX": str(bad / "cache"),
        "PYTHONDONTWRITEBYTECODE": "",
    }
    run = run_rack(rack_host, tmp_path, env=env)
    assert_clean_exit(run)
    facts = json.loads(result.read_text())
    no_markers(marker)
    assert not any(str(bad) in entry for entry in facts["path"]), facts["path"]
    assert "" not in facts["path"] and str(rack_host) not in facts["path"]  # not the working directory either
    assert facts["isolated"] and facts["ignore_environment"] and facts["safe_path"], facts
    assert facts["no_site"] and not facts["site_loaded"] and facts["no_user_site"], facts
    assert facts["dont_write_bytecode"] and facts["pycache_prefix"] is None and facts["warnings"] == [], facts
    plugin = tmp_path / "plugins-lin-x64"
    assert Path(facts["rackpy"]).is_relative_to(plugin) and Path(facts["json"]).is_relative_to(plugin)
    assert "/python/lib/python3.14/json/" in facts["json"]  # the bundled runtime's standard library


def test_plugin_files_never_replace_the_standard_library(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    marker = tmp_path / "markers"
    marker.mkdir()
    result = tmp_path / "facts.json"
    plugin = stage(
        make_bundle,
        tmp_path,
        result,
        {
            "pythonpath/json.py": evil(marker, "pythonpath-json"),
            "pythonpath/random/__init__.py": evil(marker, "pythonpath-random"),
            "pythonpath/sitecustomize.py": evil(marker, "pythonpath-sitecustomize"),
            "pythonpath/usercustomize.py": evil(marker, "pythonpath-usercustomize"),
            "pythonpath/evil.pth": pth(marker, "pythonpath-pth"),
        },
        imports=["json", "random"],
    )
    write_patch(tmp_path, [])
    run = run_rack(rack_host, tmp_path)
    assert_clean_exit(run)
    facts = json.loads(result.read_text())
    no_markers(marker)
    for name in ("json", "random"):
        assert f"/python/lib/python3.14/{name}" in facts[name], facts[name]
    pythonpath = str(plugin / "pythonpath")
    assert facts["path"][-1] == pythonpath and facts["path"].index(pythonpath) > 0  # after the standard library
    (warning,) = run.lines("named like part of Python's standard library")
    assert "json, random" in warning, warning
    assert not list(plugin.rglob("__pycache__")), "Python wrote bytecode into the installed plugin"


class _Reduce:
    """Pickles to a call of os.system: the bytes a hostile patch would carry. Only ever dumped here."""

    def __init__(self, command: str) -> None:
        self.command = command

    def __reduce__(self) -> tuple[Any, tuple[str]]:
        return (os.system, (self.command,))


KEEPER = """
import json
from pathlib import Path
from rackpy.plugin import ModuleClass, plugin

CONFIG = json.loads((Path(__file__).resolve().parent / "probe_config.json").read_text())


class Keeper(ModuleClass):
    def configure(self):
        self.module.config(0, 0, 0, 0)

    def from_json(self, data):
        Path(CONFIG["received"]).write_text(json.dumps({"data": data, "type": type(data).__name__}))


plugin.add_model("Keeper", Keeper)
import security_probe  # noqa: E402,F401
"""


def test_patches_never_run_code(make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    """Module data, patch keys and patch storage files are data: RackPy hands them over as JSON, and runs none."""
    marker = tmp_path / "markers"
    marker.mkdir()
    command = f"touch {marker / 'pickle'}"
    data = {
        "__reduce__": ["os.system", [command]],
        "py/object": "os.system",
        "!!python/object/apply:os.system": [command],
        "__class__": "subprocess.Popen",
        "code": f"__import__('os').system({command!r})",
        "pickle": base64.b64encode(pickle.dumps(_Reduce(command))).decode(),
        "nested": [{"__init__": "x", "__import__": "os"}],
    }
    result, received = tmp_path / "facts.json", tmp_path / "received.json"
    plugin = stage(
        make_bundle,
        tmp_path,
        result,
        {"pythonpath/security_keeper.py": KEEPER},
        received=str(received),
    )
    config = json.loads((plugin / "rackpy-loader.json").read_text())
    config["entry"] = "security_keeper"
    (plugin / "rackpy-loader.json").write_text(json.dumps(config))
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"].append({"slug": "Keeper", "name": "Keeper", "description": "RP-38", "tags": []})
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    module = {"id": 5, "plugin": "RackPy", "model": "Keeper", "version": "2.0.0", "params": [], "data": data}
    module["rackpy_script"] = evil(marker, "module-key")
    write_patch(tmp_path, [module])
    patch_path = tmp_path / "autosave" / "patch.json"
    patch = json.loads(patch_path.read_text())
    patch["rackpy"] = {"script": evil(marker, "patch-key"), "entry": "evil"}
    patch_path.write_text(json.dumps(patch))
    storage = tmp_path / "autosave" / "modules" / "5"
    storage.mkdir(parents=True)
    for name in ("json.py", "sitecustomize.py", "__init__.py", "security_probe.py", "evil.py"):
        (storage / name).write_text(evil(marker, "storage-" + name))
    (storage / "evil.pth").write_text(pth(marker, "storage-pth"))
    run = run_rack(rack_host, tmp_path)
    assert_clean_exit(run)
    no_markers(marker)
    assert json.loads(received.read_text()) == {"data": data, "type": "dict"}  # plain JSON, exactly as saved
    facts = json.loads(result.read_text())
    assert not any("autosave" in entry for entry in facts["path"]), facts["path"]


def write_config(plugin: Path, change: dict[str, Any] | str) -> None:
    path = plugin / "rackpy-loader.json"
    if isinstance(change, str):
        path.write_text(change)
    else:
        path.write_text(json.dumps({**json.loads(path.read_text()), **change}))


LOADER_CASES: dict[str, tuple[dict[str, Any] | str, str]] = {
    "not JSON": ("{ not json", "cannot read"),
    "a list": ("[]", "must be a JSON object"),
    "wrong ABI": ({"abi": 999}, '"abi" must be'),
    "ABI as text": ({"abi": "2"}, '"abi" must be'),
    "absolute host": ({"host": "/usr/lib/libc.so.6"}, '"host" must be a relative path'),
    "host outside": ({"host": "../../evil.so"}, '"host" must be a relative path'),
    "backslash libpython": ({"libpython": "python\\lib\\x.so"}, '"libpython" must be a relative path'),
    "python_path missing": ({"python_path": None}, '"python_path" must be a relative path'),
    "entry not a module": ({"entry": "os; import evil"}, '"entry" is not a Python module name'),
    "entry not text": ({"entry": 7}, '"entry" must be a string'),
}


@pytest.mark.parametrize("case", list(LOADER_CASES))
def test_malformed_loader_configuration_is_refused(
    case: str, make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    change, message = LOADER_CASES[case]
    plugin = stage_plugin(make_bundle(), tmp_path)
    write_config(plugin, change)
    write_patch(tmp_path, [])
    run = run_rack(rack_host, tmp_path)
    assert_clean_exit(run)
    refused = run.lines("RackPy refused to start")
    assert refused and message in refused[0], run.tail()
    assert not run.lines("RackPy interpreter started")


def test_symlinks_out_of_the_plugin_are_refused(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    """A pythonpath (or runtime) that resolves outside the plugin: RackPy never runs files from elsewhere."""
    marker, outside = tmp_path / "markers", tmp_path / "outside"
    marker.mkdir()
    outside.mkdir()
    (outside / "evil_entry.py").write_text(evil(marker, "outside-entry"))
    plugin = stage_plugin(make_bundle(), tmp_path, entry="evil_entry")
    (plugin / "elsewhere").symlink_to(outside, target_is_directory=True)
    write_config(plugin, {"python_path": "elsewhere"})
    write_patch(tmp_path, [])
    run = run_rack(rack_host, tmp_path)
    assert_clean_exit(run)
    refused = run.lines("RackPy refused to start")
    assert refused and "resolves outside the plugin folder" in refused[0], run.tail()
    no_markers(marker)
