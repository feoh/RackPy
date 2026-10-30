# SPDX-License-Identifier: GPL-3.0-or-later
"""Driver and device names must not silently discard duplicate IDs."""

import pytest

from rackpy._selection import by_name, unique_names

pytestmark = pytest.mark.unit


def test_unique_selection() -> None:
    entries = [("same", 1), ("other", 3), ("same", 2)]
    assert by_name(entries, "other", "device") == 3
    with pytest.raises(LookupError, match="not found"):
        by_name(entries, "missing", "device")
    with pytest.raises(LookupError, match="ambiguous; select by ID"):
        by_name(entries, "same", "device")
    with pytest.raises(LookupError, match="ambiguous; enumerate and select by ID"):
        unique_names(entries, "device")
    assert unique_names([("one", 1), ("two", 2)], "device") == {"one": 1, "two": 2}
