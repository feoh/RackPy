# SPDX-License-Identifier: GPL-3.0-or-later
"""Static facade contracts: mypy checks these without starting a Rack host."""

from typing import TYPE_CHECKING, assert_type

if TYPE_CHECKING:
    from rackpy.audio import open_port
    from rackpy.midi import open_input
    from rackpy.raw.audio import Port
    from rackpy.raw.midi import Input, InputQueue
    from rackpy.services.network import delete_json, get_json, post_json, put_json
    from rackpy.types import JSON

    class CustomAudio(Port): ...

    class CustomMidi(Input): ...

    assert_type(open_port(1), Port)
    assert_type(open_port(1, kind=CustomAudio), CustomAudio)
    assert_type(open_input(1), InputQueue)
    assert_type(open_input(1, kind=CustomMidi), CustomMidi)
    body: JSON = {"value": 1, "items": [True, None]}
    assert_type(get_json("https://example.invalid"), JSON)
    assert_type(post_json("https://example.invalid", body), JSON)
    assert_type(put_json("https://example.invalid", body), JSON)
    assert_type(delete_json("https://example.invalid"), JSON)
