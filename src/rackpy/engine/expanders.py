# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's module expanders and RackPy's expander messages (RP-23A).

Available inside Rack only. A module's ``left_expander`` and
``right_expander`` are :class:`Expander` views. Their message slots hold
:class:`ExpanderMessage` buffers that RackPy allocated, opaque
:class:`ForeignMessage` pointers that another plugin allocated, or ``None``.
Python touches message slots, message contents and the flip flag only while
Rack's engine does not run the module; native kernels exchange messages
while it does: :func:`graph_messages` gives a side the messages that DSP
graphs exchange (RP-23B). See ``docs/reference/engine-module.md#expanders``.
"""

from rackpy.dsp.graph import EXPANDER_SCHEMA, LANE, MAX_LANES
from rackpy.raw import engine as _engine

Expander = _engine.Module.Expander
ExpanderMessage = _engine.ExpanderMessage
ForeignMessage = _engine.ForeignMessage


def allocate_messages(expander: Expander, schema: str, size: int) -> tuple[ExpanderMessage, ExpanderMessage]:
    """Give one side of a RackPy module its two message buffers, as a Rack module allocates them.

    Rack swaps a side's producer and consumer buffers when a flip is requested, so
    both have the same schema and size. Returns ``(producer, consumer)``; the
    module keeps both alive from then on. Raises what the slots raise: on
    another plugin's module, or while Rack's engine runs the module.
    """
    producer, consumer = ExpanderMessage(schema, size), ExpanderMessage(schema, size)
    previous = expander.producer_message
    expander.producer_message = producer
    try:
        expander.consumer_message = consumer
    except BaseException:
        expander.producer_message = previous if isinstance(previous, ExpanderMessage) else None
        raise
    return producer, consumer


def graph_messages(expander: Expander, lanes: int = 1) -> tuple[ExpanderMessage, ExpanderMessage]:
    """Gives one side of a RackPy module the ``"rackpy.graph"`` messages DSP graphs exchange (RP-23B).

    Each of the ``lanes`` lanes carries one signal (``rackpy.dsp.graph.LANE``
    values: the channel count, then the voltages). A module receives in its
    own messages: the neighbour's ``Graph.expander_send`` writes into them, and
    this module's ``Graph.expander_receive`` reads them one frame later.
    """
    if isinstance(lanes, bool) or not isinstance(lanes, int) or not 1 <= lanes <= MAX_LANES:  # pyright: ignore[reportUnnecessaryIsInstance]
        raise ValueError(f"graph_messages takes 1 to {MAX_LANES} lanes, not {lanes!r}")
    return allocate_messages(expander, EXPANDER_SCHEMA, lanes * LANE)


__all__ = ["Expander", "ExpanderMessage", "ForeignMessage", "allocate_messages", "graph_messages"]
