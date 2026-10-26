# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-23A: Rack's module expanders and RackPy's expander messages, inside Rack (live UI, test hooks).

Expander views on RackPy modules and on another plugin's module (the
RuntimeProbe test module); RackPy message buffers, their schemas, ownership
and identity; the rule that Python touches message pointers, contents and
the flip flag only while Rack's engine does not run the module; neighbours
found by ID while it does; and flips on a laboratory Bench.
"""

import gc
from collections.abc import Iterator

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import laboratory, raw
from rackpy.errors import OwnershipError, StaleHandleError
from rackpy.laboratory import Bench, ModuleClass
from rackpy.raw.engine import EngineModule, ExpanderMessage, Module


def owned() -> Module:
    return Module.create(lambda m: m.config(0, 0, 1, 0))


def owner(message: ExpanderMessage) -> Module | None:
    """A fresh read of message.owner (mypy would otherwise keep a narrowed attribute across calls)."""
    return message.owner


def test_expander_views() -> None:
    m = owned()
    try:
        left, right = m.left_expander, m.right_expander
        assert (left.side, right.side) == (0, 1) and repr(right) == "<Module.Expander right>"
        assert m.get_left_expander().side == 0 and m.get_right_expander().side == 1
        assert m.get_expander(0).side == 0 and m.get_expander(1).side == 1 and m.get_expander(7).side == 1
        with pytest.raises(ValueError, match="uint8_t"):
            m.get_expander(256)
        for e in (left, right):
            assert e.module_id == -1 and e.module is None
            assert e.producer_message is None and e.consumer_message is None and not e.message_flip_requested
        right.request_message_flip()
        assert right.message_flip_requested and not left.message_flip_requested  # Rack's own flag, per side
        right.message_flip_requested = False
        right.module_id = 42
        assert m.right_expander.module_id == 42  # a view: the module's field
        with pytest.raises(ValueError, match="-1 for none"):
            right.module_id = -2
    finally:
        m.close()
    with pytest.raises(StaleHandleError):
        right.module_id  # noqa: B018 - the property raises


def test_expander_messages() -> None:
    msg = ExpanderMessage("demo.Mix", 4)
    assert (msg.schema, msg.size, len(msg), msg.owner) == ("demo.Mix", 4, 4, None)
    assert msg.tolist() == [0.0] * 4 and repr(msg) == "<ExpanderMessage demo.Mix[4]>"
    msg[1] = 2.5
    msg[-1] = -1.0
    assert msg[1] == 2.5 and msg.tolist() == [0.0, 2.5, 0.0, -1.0]
    msg.write([1.0, 2.0, 3.0, 4.0])
    assert msg.tolist() == [1.0, 2.0, 3.0, 4.0]
    with pytest.raises(IndexError, match="4 floats"):
        msg[4]
    with pytest.raises(ValueError, match="holds 4 floats, not 3"):
        msg.write([1.0, 2.0, 3.0])
    for schema, size, match in (
        ("", 1, "schema"),
        ("two words", 1, "schema"),
        ("x" * 65, 1, "schema"),
        ("ok", 0, "at least 1"),
        ("ok", 65537, "65536"),
    ):
        with pytest.raises(ValueError, match=match):
            ExpanderMessage(schema, size)


def test_a_module_owns_the_messages_in_its_slots() -> None:
    m, other = owned(), owned()
    try:
        right = m.right_expander
        a, b = ExpanderMessage("demo.Mix", 4), ExpanderMessage("demo.Mix", 4)
        a.write([1.0, 2.0, 3.0, 4.0])
        right.producer_message = a
        right.consumer_message = b
        assert right.producer_message is a and right.consumer_message is b  # the same objects
        assert a.owner is m and b.owner is m
        with pytest.raises(ValueError, match="other slot of this side already holds"):
            right.producer_message = b
        with pytest.raises(ValueError, match="same schema and size"):
            right.producer_message = ExpanderMessage("demo.Mix", 5)
        with pytest.raises(OwnershipError, match="another module's expander"):
            other.left_expander.producer_message = a
        with pytest.raises(OwnershipError, match="another expander slot of this module"):
            m.left_expander.producer_message = a
        with pytest.raises(TypeError, match="ExpanderMessage or None"):
            right.producer_message = 7  # type: ignore[assignment]
        del a
        gc.collect()
        kept = right.producer_message
        assert isinstance(kept, ExpanderMessage) and kept.tolist() == [1.0, 2.0, 3.0, 4.0]  # the module kept it alive
        right.producer_message = None
        assert owner(kept) is None and m.right_expander.producer_message is None
        other.left_expander.producer_message = kept  # free again
        assert other.left_expander.producer_message is kept and owner(kept) is other
    finally:
        m.close()
        other.close()
    assert owner(kept) is None  # its module is gone; the message lives on with its Python handle


Pair = tuple[Module, Module, tuple[ExpanderMessage, ExpanderMessage]]


@pytest.fixture
def engine_pair() -> Iterator[Pair]:
    left = owned()
    messages = ExpanderMessage("demo.Pair", 2), ExpanderMessage("demo.Pair", 2)
    left.right_expander.producer_message, left.right_expander.consumer_message = messages
    neighbour_id = HOOKS.engine_adopt(owned_right := owned())
    left.right_expander.module_id = neighbour_id  # as Rack's rack does for adjacent widgets
    HOOKS.engine_adopt(left)
    try:
        yield left, owned_right, messages
    finally:
        HOOKS.engine_delete(left, None)
        HOOKS.engine_delete(owned_right, None)


def test_python_keeps_off_what_rack_engine_thread_uses(engine_pair: Pair) -> None:
    left, neighbour, _ = engine_pair
    right = left.right_expander
    assert right.module_id == neighbour.id and right.module is neighbour  # found by ID through Rack's engine
    assert neighbour.left_expander.module is None
    for touch in (
        lambda: right.producer_message,
        lambda: right.consumer_message,
        lambda: right.message_flip_requested,
        right.request_message_flip,
        lambda: setattr(right, "module_id", 3),
        lambda: setattr(right, "consumer_message", None),
    ):
        with pytest.raises(OwnershipError, match="engine runs this module"):
            touch()
    assert left.right_expander.module_id == neighbour.id  # the ID stays readable
    message = ExpanderMessage("demo.Pair", 2)
    with pytest.raises(OwnershipError, match="engine runs this module"):
        left.left_expander.producer_message = message


def test_messages_in_a_running_module_are_off_limits(engine_pair: Pair) -> None:
    left, _, messages = engine_pair
    for message in messages:
        assert message.owner is left
        with pytest.raises(OwnershipError, match="engine runs the module whose expander holds"):
            message.tolist()
        with pytest.raises(OwnershipError, match="engine runs the module whose expander holds"):
            message[0] = 1.0
        with pytest.raises(OwnershipError, match="engine runs the module whose expander holds"):
            message.write([1.0, 2.0])


def test_another_plugins_module() -> None:
    handle = HOOKS.add_probe_module()
    try:
        context = raw.context_get()
        assert context is not None and context.engine is not None
        probe = context.engine.get_module(handle.module_id)
        assert isinstance(probe, EngineModule)
        e = probe.right_expander
        assert e.side == 1 and e.module_id == -1 and e.module is None
        with pytest.raises(OwnershipError, match="engine runs this module"):
            e.producer_message  # noqa: B018 - the property raises
    finally:
        HOOKS.remove_probe_module(handle)
    with pytest.raises(StaleHandleError):
        e.module_id  # noqa: B018 - the property raises


def test_a_laboratory_bench_flips_requested_messages() -> None:
    class Plain(ModuleClass):
        def configure(self) -> None:
            self.module.config(0, 0, 1, 0)

    laboratory.enable()
    try:
        bench = Bench(48000)
        a, b = Plain.create(), Plain.create()
        bench.add(a.module)
        bench.add(b.module)
        bench.set_expander(a.module, 1, b.module)
        bench.step()
        right = a.module.right_expander
        assert right.module is b.module and right.module_id == b.module.id
        producer, consumer = ExpanderMessage("demo.Bench", 1), ExpanderMessage("demo.Bench", 1)
        right.producer_message, right.consumer_message = producer, consumer
        producer[0] = 5.0
        bench.step()
        assert right.producer_message is producer  # no flip requested
        right.request_message_flip()
        bench.step()  # Rack's engine swaps at the end of the frame
        assert right.producer_message is consumer and right.consumer_message is producer
        assert right.consumer_message[0] == 5.0 and not right.message_flip_requested
        bench.remove(b.module)
        assert a.module.right_expander.module is None and a.module.right_expander.module_id == -1
        bench.clear()
    finally:
        laboratory.disable()
