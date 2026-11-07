"""Small Python reference for tests and teaching; never used in Rack's audio path."""

from __future__ import annotations

import math
from math import ceil


class ADReference:
    """Frame-at-a-time reference for the native envelope's attack/decay stages."""

    def __init__(self) -> None:
        self.stage = "idle"
        self.value = 0.0
        self.trigger_state = "uninitialized"
        self.pulse_frames = 0

    @staticmethod
    def _seconds(knob: float) -> float:
        return float(0.001 * 2000.0 ** min(1.0, max(0.0, knob)))

    def process(
        self, trigger: float, attack: float, decay: float, loop: bool, sample_rate: int
    ) -> tuple[float, float, float]:
        if self.trigger_state == "uninitialized":
            if trigger >= 1.0:
                self.trigger_state = "high"
            elif trigger <= 0.0:
                self.trigger_state = "low"
        elif self.trigger_state == "low" and trigger >= 1.0:
            self.trigger_state = "high"
            self.stage = "attack"
            self.pulse_frames = 0
        elif self.trigger_state == "high" and trigger <= 0.0:
            self.trigger_state = "low"
        if self.stage == "attack":
            coefficient = 1.0 - math.exp(-1.0 / (self._seconds(attack) * sample_rate))
            self.value += (10.0 - self.value) * coefficient
            if self.value >= 9.99:
                self.value = 10.0
                self.stage = "decay"
        elif self.stage == "decay":
            coefficient = 1.0 - math.exp(-1.0 / (self._seconds(decay) * sample_rate))
            self.value *= 1.0 - coefficient
            if self.value <= 0.01:
                self.value = 0.0
                self.pulse_frames = max(1, ceil(sample_rate * 0.001))
                self.stage = "attack" if loop else "idle"
        eoc = 10.0 if self.pulse_frames > 0 else 0.0
        if self.pulse_frames > 0:
            self.pulse_frames -= 1
        return self.value, 10.0 - self.value, eoc
