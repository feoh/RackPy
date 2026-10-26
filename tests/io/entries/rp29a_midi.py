# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-29A test entry module, in windowed Rack: a Python Input's queued on_message, overflow, and ports
still alive when Rack quits."""

import json
import traceback
from collections.abc import Generator
from pathlib import Path
from typing import Any

from _rackpy_host import _testing

from rackpy.errors import ContextError
from rackpy.raw import midi

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "rp29a_config.json").read_text())
RESULT: dict[str, Any] = {"delivered": [], "threads": set()}
LOOPBACK = -12

try:
    midi.Input()  # plugin start-up: no live context yet
    RESULT["startup"] = "allowed"
except ContextError:
    RESULT["startup"] = "refused"


class Recorder(midi.Input):
    def on_message(self, message: midi.Message) -> None:
        import threading

        RESULT["delivered"].append(message.get_note())
        RESULT["threads"].add(threading.current_thread() is threading.main_thread())


KEEP: list[Any] = []


def note(n: int) -> midi.Message:
    message = midi.Message()
    message.set_status(0x9)
    message.set_note(n)
    message.set_value(1)
    return message


def scenario() -> Generator[None]:
    out, rec = midi.Output(), Recorder()
    for port in (out, rec):
        port.set_driver_id(LOOPBACK)
        port.set_device_id(3)
    for n in range(5):
        out.send_message(note(n))
    RESULT["before_step"] = list(RESULT["delivered"])  # queued, not yet delivered
    yield
    yield
    RESULT["after_step"] = list(RESULT["delivered"])
    RESULT["delivered"].clear()
    for n in range(2000):
        out.send_message(note(n % 128))
    yield
    yield
    RESULT["overflow"] = [len(RESULT["delivered"]), rec.dropped]
    KEEP.extend([out, rec])  # still alive when Rack quits: RackPy releases them while the drivers exist


running: list[Generator[None]] = []
done = [False]


def finish() -> None:
    RESULT["threads"] = sorted(RESULT["threads"])
    Path(CONFIG["result"]).write_text(json.dumps(RESULT))
    _testing.close_window()


def on_step(frame: int, handle: object) -> None:
    if done[0] or frame < 3:
        return
    try:
        if not running:
            running.append(scenario())
        next(running[0])
    except StopIteration:
        done[0] = True
        finish()
    except BaseException:
        RESULT["error"] = traceback.format_exc()
        done[0] = True
        finish()


_testing.set_step_callback(on_step)
