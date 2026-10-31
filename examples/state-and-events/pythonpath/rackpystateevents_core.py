"""The sequencer's processing and its saved formats. This module does not import Rack.

The graph is what Rack's engine runs, natively, once compiled; everything else here
turns the module's Python state into saved data and back. Saved data comes from patch
files, patch storage and Rack's settings.json, which anyone may have edited, so each
reader takes only values it understands and ignores the rest.
"""

import math
import struct
from dataclasses import dataclass, field
from typing import TYPE_CHECKING

from rackpy.dsp.graph import Graph

if TYPE_CHECKING:  # rackpy.types needs Rack; annotations are not evaluated at run time
    from rackpy.types import JSON

STEPS = 16
LENGTH = 0  # param
CLOCK, RESET = 0, 1  # inputs
CV = 0  # output
STEP_LIGHT = 0  # light

RANGES = (1.0, 2.0, 5.0, 10.0)  # full scale in volts; the module's "range" is an index
RANGE_LABELS = ("0-1 V", "0-2 V", "0-5 V", "0-10 V")
DEFAULT_RANGE = 0
LENGTHS = (4, 8, 12, 16)  # what Randomize chooses from
DEFAULT_LENGTH = 8

STATE_VERSION = 1
PATTERN_FILE = "pattern.f32"  # STEPS little-endian float32 values, each in [0, 1]
_PATTERN = struct.Struct(f"<{STEPS}f")


def step_name(i: int) -> str:
    return f"step{i}"


def default_pattern() -> list[float]:
    """A rising ramp, the pattern of a new or initialized sequencer."""
    return [i / (STEPS - 1) for i in range(STEPS)]


def sequencer_graph() -> Graph:
    """Advance one step per CLOCK rise, wrap at LENGTH, restart on RESET; CV is the step's value times the range.

    Each step's value and the range are controls that Python sets from the module's
    state; the step counter is the kernel's own, so Initialize (which resets the
    kernel) restarts it at the first step.
    """
    g = Graph()
    clock = g.schmitt(g.input(CLOCK), 0.1, 1.0)
    reset = g.schmitt(g.input(RESET), 0.1, 1.0)
    step = g.delay(0.0)
    following = step + 1.0
    advanced = g.where(clock, g.where(following >= g.param(LENGTH), 0.0, following), step)
    current = g.where(reset, 0.0, advanced)
    step.feed(current)
    value = sum(
        (g.where(abs(current - i) < 0.5, g.control(step_name(i), default_pattern()[i]), 0.0) for i in range(STEPS)),
        g.constant(0.0),
    )
    g.output(CV, value * g.control("range", RANGES[DEFAULT_RANGE]))
    g.light(STEP_LIGHT, g.pulse(clock, 0.05))
    return g


@dataclass
class State:
    """What a module saves in the patch: its range. The pattern goes to patch storage."""

    range: int = DEFAULT_RANGE
    pattern: list[float] = field(default_factory=default_pattern)

    def to_json(self) -> dict[str, int]:
        return {"version": STATE_VERSION, "range": self.range}

    def load_json(self, data: JSON) -> None:
        """Take what this version understands. Missing or invalid fields keep their values; unknown ones are ignored.

        A newer version's data is read the same way: its version number is not a
        reason to refuse fields this version knows.
        """
        if not isinstance(data, dict):
            return
        chosen = data.get("range")
        if isinstance(chosen, int) and not isinstance(chosen, bool) and 0 <= chosen < len(RANGES):
            self.range = chosen

    def pattern_bytes(self) -> bytes:
        return _PATTERN.pack(*self.pattern)

    def load_pattern(self, data: bytes) -> bool:
        """Take a pattern file's values; a file of the wrong size is ignored. NaN reads as 0; values are clamped."""
        if len(data) != _PATTERN.size:
            return False
        self.pattern = [min(max(v, 0.0), 1.0) if not math.isnan(v) else 0.0 for v in _PATTERN.unpack(data)]
        return True


@dataclass
class Settings:
    """Plugin-wide: the length a new or initialized sequencer starts with. Kept in Rack's settings.json."""

    default_length: int = DEFAULT_LENGTH

    def to_json(self) -> dict[str, int]:
        return {"defaultLength": self.default_length}

    def load_json(self, data: JSON) -> None:
        if not isinstance(data, dict):
            return
        length = data.get("defaultLength")
        if isinstance(length, int) and not isinstance(length, bool) and 1 <= length <= STEPS:
            self.default_length = length
