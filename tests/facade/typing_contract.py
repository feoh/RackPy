# SPDX-License-Identifier: GPL-3.0-or-later
"""Static facade contracts: mypy checks these without starting a Rack host."""

from typing import TYPE_CHECKING, assert_type

if TYPE_CHECKING:
    from rackpy.audio import open_port
    from rackpy.midi import open_input
    from rackpy.raw.audio import Port
    from rackpy.raw.midi import Input, InputQueue

    class CustomAudio(Port): ...

    class CustomMidi(Input): ...

    assert_type(open_port(1), Port)
    assert_type(open_port(1, kind=CustomAudio), CustomAudio)
    assert_type(open_input(1), InputQueue)
    assert_type(open_input(1, kind=CustomMidi), CustomMidi)
