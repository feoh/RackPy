# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.engine import Module


def save_table(module: Module, table: list[float]) -> None:
    """Call on Rack's main thread for a module Rack's engine has added, e.g. when the patch is saved."""
    path = module.patch_storage() / "table.txt"
    path.write_text("\n".join(map(str, table)))


def load_table(module: Module) -> list[float]:
    """Call on Rack's main thread once the module is added; an empty table if nothing was saved."""
    path = module.patch_storage(create=False) / "table.txt"
    if not path.is_file():
        return []
    return [float(line) for line in path.read_text().splitlines()]
