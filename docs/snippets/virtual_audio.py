# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from array import array

from rackpy import audio
from rackpy.raw import audio as raw_audio

DRIVER_ID = 7001  # stored in patches: pick one no other driver uses


class Peak(raw_audio.Port):
    """A port that keeps the loudest input sample of each block a Python device delivers."""

    def __init__(self) -> None:
        super().__init__()
        self.peaks: list[float] = []

    def process_input(self, input: array[float], input_stride: int, frames: int) -> None:
        self.peaks.append(max((abs(x) for x in input), default=0.0))


def render(blocks: int) -> list[float]:
    """Call from Rack's UI thread once: a virtual device streams a ramp to a Peak port, one block at a time."""
    driver = audio.virtual_driver(DRIVER_ID, "Docs Virtual", devices=("Ramp",))
    device = driver.get_device(0)
    assert device is not None
    port = audio.open_port(DRIVER_ID, "Ramp", kind=Peak)
    with audio.Stream(device) as stream:
        for block in range(blocks):
            frames = device.block_size
            ramp = array("f", [block / blocks] * (frames * device.num_inputs))
            stream.process(frames, ramp)
    port.set_device_id(-1)
    return port.peaks
