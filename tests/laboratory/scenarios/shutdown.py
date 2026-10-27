# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep laboratory objects/cycles alive across Rack context and interpreter teardown."""

import threading
from typing import Any

import _rackpy_host as host

import rackpy._bootstrap
from rackpy import laboratory, runtime
from rackpy.errors import StaleHandleError, ThreadError
from rackpy.raw.audio import Port as AudioPort
from rackpy.raw.midi import Output

RELEASE_ON_CLOSE = False  # the outer subprocess test also runs a cleanup variant
laboratory.enable()


class Cyclic(laboratory.ModuleClass):
    def configure(self) -> None:
        self.module.config(0, 0, 0, 0)
        self.bench = bench

    def process(self, args: laboratory.ProcessArgs) -> None:
        counts["callbacks"] += 1


class Value(laboratory.Quantity):
    def get_value(self) -> float:
        return 2.0


bench = laboratory.Bench()
instance = Cyclic.create()
bench.add(instance.module)
quantity = Value()
generator = laboratory.MidiGenerator[1]()
device = laboratory.MidiOutputDevice()
output: Output | None = None  # ports need Rack's live context, unavailable at startup
teardown_calls: list[str] = []  # Python overrides that ran after the closing hook (RP-22F: none may)


class AudioDriver(laboratory.AudioDriver):
    def unsubscribe(self, device_id: int, port: AudioPort) -> None:
        teardown_calls.append("audio unsubscribe")
        super().unsubscribe(device_id, port)


class MidiDriver(laboratory.MidiDriver):
    def unsubscribe_input(self, device_id: int, input: Any) -> None:
        teardown_calls.append("midi unsubscribe_input")
        super().unsubscribe_input(device_id, input)


audio_driver = AudioDriver("shutdown")
audio_device = laboratory.AudioDevice("held")
audio_driver.add_device(audio_device)
midi_driver = MidiDriver("shutdown")
midi_device = laboratory.MidiInputDevice("held")
midi_driver.add_input_device(midi_device)
connected: list[Any] = []  # the live-context run's laboratory ports, kept through shutdown
counts = {"callbacks": 0, "refused": 0}
bench.step()
stop = threading.Event()
attempted = threading.Event()


def worker() -> None:
    while not stop.is_set():
        try:
            bench.step()
        except ThreadError:
            counts["refused"] += 1
            attempted.set()
        stop.wait(0.001)


thread = threading.Thread(target=worker)
thread.start()
assert attempted.wait(5), "worker never attempted a laboratory call"


@runtime.on_context_closing
def closing() -> None:
    # The main thread can still use the laboratory while shutdown starts;
    # the competing worker must never enter Python process().
    bench.step()
    stop.set()
    thread.join(timeout=5)
    assert not thread.is_alive(), "laboratory worker did not stop"
    assert counts["refused"] > 0 and counts["callbacks"] == 2, counts
    assert quantity.get_display_value() == 2.0
    assert bench.modules == (instance.module,)
    assert (output is not None) == host.TEST_HOOKS
    if connected:
        audio_port, midi_input = connected
        assert audio_device.subscribed == [audio_port] and midi_device.subscribed == [midi_input]
    teardown_calls.clear()
    if RELEASE_ON_CLOSE:
        bench.clear()
        instance.module.close()  # release native override/data-handler PyRefs before finalization
        host.log_info("LAB SHUTDOWN holders released")
    else:
        # Live globals keep the native override/data-handler references alive.
        # Runtime policy must refuse finalization, not free Python under them.
        host.log_info("LAB SHUTDOWN holders retained")
    host.log_info("LAB SHUTDOWN closing hook passed")


bootstrap_stop = rackpy._bootstrap.stop


def stopping() -> None:
    """Rack's context closed: every laboratory port gave its devices back, natively."""
    if connected:
        for port in connected:
            try:
                port.get_device_id()
            except StaleHandleError:
                pass
            else:
                raise AssertionError("a laboratory port outlived Rack's context")
            assert host._testing.port_targets(port) == (False, False, -1, -1), host._testing.port_targets(port)
        assert audio_device.subscribed == [] and midi_device.subscribed == [] and teardown_calls == []
        host.log_info("LAB SHUTDOWN laboratory drivers disconnected without Python")
    bootstrap_stop()


# The only Python that runs after Rack's context closes is rackpy._bootstrap.stop (before finalization).
rackpy._bootstrap.stop = stopping
host.log_info("LAB SHUTDOWN armed")
if host.TEST_HOOKS:

    def on_step(frame: int, handle: object) -> None:
        global output  # noqa: PLW0603 - retain the connected port through context shutdown
        if frame >= 3 and output is None:
            output = Output()
            device.connect(output)
            audio_port, midi_input = AudioPort(), laboratory.MidiInput()
            audio_driver.connect(audio_port)
            midi_driver.connect(midi_input)
            connected.extend((audio_port, midi_input))
            host.log_info("LAB SHUTDOWN output connected in live context")
            host._testing.close_window()

    host._testing.set_step_callback(on_step)
