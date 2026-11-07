# SPDX-License-Identifier: GPL-3.0-or-later
"""Small scalar reference for offline tests; production processing is governor.cpp."""

from dataclasses import dataclass
from math import exp, pi


@dataclass
class FeedbackGovernorReference:
    low: float = 0.0
    dc_input: float = 0.0
    dc_output: float = 0.0

    def reset(self) -> None:
        self.low = self.dc_input = self.dc_output = 0.0

    def process(
        self,
        send: float,
        returned: float,
        *,
        feedback: float,
        cap: float,
        tone: float,
        decay: float,
        cv: float = 0.0,
        kill: bool = False,
        sample_rate: float = 48000.0,
    ) -> float:
        if kill:
            self.reset()
            return 0.0
        rate = max(sample_rate, 1.0)
        cutoff = min(max(tone, 20.0), 20000.0)
        alpha = 1.0 - exp(-2.0 * pi * min(cutoff, 0.45 * rate) / rate)
        self.low += alpha * (returned - self.low)
        pole = exp(-2.0 * pi * 5.0 / rate)
        blocked = self.low - self.dc_input + pole * self.dc_output
        self.dc_input, self.dc_output = self.low, blocked
        amount = min(max(feedback + min(max(cv * 0.1, 0.0), 1.0), 0.0), 1.0)
        governed = blocked * amount * min(max(decay, 0.0), 1.0)
        ceiling = min(max(cap, 1.0), 10.0)
        return min(max(send + governed, -ceiling), ceiling)
