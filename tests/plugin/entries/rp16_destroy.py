# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-16B test entry module: destroy() callbacks, newest first, one of them failing."""

import _rackpy_host

from rackpy.plugin import on_destroy


@on_destroy
def first() -> None:
    _rackpy_host.log_info("rp16-destroy first")


@on_destroy
def failing() -> None:
    raise RuntimeError("rp16-destroy-intentional-error")


@on_destroy
def last() -> None:
    _rackpy_host.log_info("rp16-destroy last")
