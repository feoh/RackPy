# SPDX-License-Identifier: GPL-3.0-or-later
"""NVGcolor and rackpy.raw.color inside Rack: an immutable value, Rack's parsing and constants."""

import copy

import pytest

from rackpy.raw import color
from rackpy.raw.nanovg import NVGcolor
from rackpy.types import Color


def test_nvgcolor_is_an_immutable_hashable_value() -> None:
    c = NVGcolor(0.25, 0.5, 0.75)
    assert c.rgba == (0.25, 0.5, 0.75, 1.0) and (c.r, c.g, c.b, c.a) == c.rgba
    with pytest.raises(AttributeError):
        c.r = 1.0  # type: ignore[misc]
    assert c == NVGcolor(0.25, 0.5, 0.75, 1.0) and c != NVGcolor(0.25, 0.5, 0.75, 0.5)
    assert len({c, NVGcolor(0.25, 0.5, 0.75)}) == 1
    assert copy.copy(c) == c and repr(c) == "NVGcolor(0.25, 0.5, 0.75, 1.0)"
    assert Color is NVGcolor


def test_constants_cannot_be_changed() -> None:
    red = color.RED
    with pytest.raises(AttributeError):
        red.g = 1.0  # type: ignore[misc]
    assert NVGcolor(1, 0, 0) == color.RED and NVGcolor(1, 1, 1, 0) == color.WHITE_TRANSPARENT


def test_hex_strings_follow_rack() -> None:
    assert color.from_hex_string("#ff8000") == NVGcolor(1, 128 / 255, 0)
    # color.hpp says WHITE on error, but Rack's sscanf-based code returns what it parsed: never raises.
    assert color.from_hex_string("not a colour") == color.BLACK
    assert color.from_hex_string("#ff") == color.RED
    assert color.to_hex_string(NVGcolor(1, 0, 0)) == "#ff0000"
    assert color.to_hex_string(NVGcolor(1, 0, 0, 0.5)) == "#ff000080"


def test_mult_overloads_are_reached_by_argument_type() -> None:
    a = NVGcolor(0.5, 0.5, 0.5, 0.5)
    assert color.mult(a, 2.0) == NVGcolor(1, 1, 1, 0.5)
    assert color.mult(a, NVGcolor(0.5, 1, 0, 1)) == NVGcolor(0.25, 0.5, 0, 0.5)
    with pytest.raises(TypeError):
        color.mult(a, "x")  # type: ignore[call-overload]
