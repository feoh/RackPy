# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy import raw
from rackpy.raw.engine import EngineModule


def describe_engine() -> list[str]:
    """Call from Rack's UI thread once its context exists, not at plugin startup."""
    context = raw.context_get()
    assert context is not None and context.engine is not None
    engine = context.engine
    lines = [f"{engine.get_sample_rate():.0f} Hz, {engine.get_num_modules()} modules"]
    for module_id in engine.get_module_ids():
        module = engine.get_module(module_id)
        if isinstance(module, EngineModule):  # another plugin's module
            kind = "bypassed" if module.is_bypassed() else "running"
            slug = module.model.slug if module.model else "?"
            lines.append(f"{slug} {module_id}: {kind}, {module.get_num_params()} params")
    return lines
