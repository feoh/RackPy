# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38: the last test leaves everything live, so that Rack shuts the interpreter down around it.

Rack quits after this suite: it deletes the scene and the engine with these modules
running, expanders paired, a menu open, kernel controls queued, and Python objects
holding handles to all of them. The outer test checks that Rack exits cleanly.
"""

import itertools

import stress_models as sm
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.engine import Module

LEFT_LIVE: list[object] = []  # kept past the test: the handles Python still holds while Rack quits


def test_leave_everything_running_for_shutdown() -> None:
    modules: list[Module] = []
    for _ in range(8):
        module = HOOKS.engine_handle(HOOKS.browser_add_module("RackPy", "Churn"))
        assert module is not None
        modules.append(module)
    for a, b in itertools.pairwise(modules):
        # No engine cables: Rack's scene deletes cables with their widgets, and these would have none.
        HOOKS.rack_set_expander_id(a, 1, b.id)
        HOOKS.rack_set_expander_id(b, 0, a.id)
    for module in modules:
        churn = sm.Churn.of(module)
        for value in range(10):
            churn.kernel.set("amp", float(value))
        LEFT_LIVE.extend([module, churn, churn.kernel, module.params[0], module.outputs[0]])
    menu = raw.create_menu()
    menu.add_child(raw.create_menu_label("open while Rack quits"))
    LEFT_LIVE.append(menu)
    HOOKS.engine_bypass(modules[0], True)
