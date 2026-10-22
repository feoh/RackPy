# SPDX-License-Identifier: GPL-3.0-or-later
# Stub for the native rackpy.raw._types (native/bindings/values/quantity.cpp).
from rackpy.raw import Quantity

class ValueQuantity(Quantity):
    min_value: float
    max_value: float
    default_value: float
    label: str
    unit: str
    def __init__(
        self,
        min_value: float = 0.0,
        max_value: float = 1.0,
        default_value: float = 0.0,
        label: str = "",
        unit: str = "",
    ) -> None: ...
