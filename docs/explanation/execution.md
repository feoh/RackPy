# How Rack runs a module, and where Python fits

This page explains the model behind RackPy's rules: how Rack's engine advances
time, why a module's audio cannot be Python, and what Python does instead.

## Rack advances one sample at a time

Rack's engine computes the whole patch one **frame** at a time: one sample of
every signal, 48,000 frames a second at 48 kHz. Each frame, in this order
(Rack 2.6.6, `Engine_stepFrame`):

```text
1. every module processes: reads its inputs, knobs and expanders' messages,
   writes its outputs and lights            (on one or more engine threads)
2. every cable copies its output's voltages to its input
   (a non-finite value arrives as 0 V)
3. every expander message that was sent is swapped to its receiver
```

Two consequences matter for anything that processes audio:

- **Every cable delays by one sample.** What a module writes in frame *n*
  reaches the input at the other end in frame *n + 1*. A loop of cables is
  therefore always allowed, with one sample of delay per cable.
- **Feedback inside a module needs an explicit delay**, for the same reason:
  a graph's {py:meth}`~rackpy.dsp.graph.Graph.delay` is that sample, and RackPy
  refuses a cycle without one rather than add latency
  ([feedback](../how-to/graphs.md#feedback)).

Modules in one frame may run in any order, or at the same time on several
threads, so a module must not depend on reading another's state directly.
Expander messages arrive exactly one frame later for the same reason
([expanders](../how-to/expanders.md)).

## Why a module's audio is native

A frame at 48 kHz lasts about 21 microseconds, for the whole patch. Python
cannot meet that reliably: it allocates memory, it holds a global lock that
another thread can keep for milliseconds, and its garbage collector pauses.
Any of these on Rack's engine thread would be a dropout, for every module in
the patch, not only the Python one. So Python never runs there
([callbacks](callbacks.md#why-python-never-runs-on-racks-audio-threads)).

Instead, a module's processing is a **kernel**: native code that Rack's engine
calls each frame, as it calls a C++ module's `process()`. It is made one of two
ways:

| Kernel from | Written as | Good for |
| --- | --- | --- |
| A [graph](../how-to/graphs.md) | Python that describes nodes and connections, once | oscillators, filters, mixing, logic: most modules, polyphonic for free |
| [C++](../how-to/dsp.md#write-a-kernel-in-c) | a C++ class in a kernel library shipped with the plugin | loops a graph cannot express, reading a neighbour module, SIMD |

Python hands a running kernel new values through **controls**, from the main
thread: they arrive at the next frame, through a fixed-size queue that never
blocks the engine.

## What Python does

```text
Rack's main thread (Python runs here)        Rack's engine threads (never Python)
--------------------------------------       -----------------------------------
build modules: configure(), set_kernel()  -> kernel runs every frame
draw panels, menus, events, saving        -> controls reach the kernel next frame
read outputs and lights between frames    <- what the kernel wrote
```

Python runs on Rack's **main thread**, where Rack builds modules, draws the
window, opens menus, handles events and saves patches, and on threads you
start. It configures modules, draws their panels and keeps their state. A slow
callback there makes the interface less responsive; it does not stop the audio,
except in the few callbacks Rack itself runs with the engine paused (reset,
randomize, bypass and loading a module's JSON), which RackPy times
([troubleshooting](../troubleshooting.md#the-audio-stutters)).

## Voltages

Rack's signals are numbers of volts with the conventions of hardware modular
synthesizers: audio at about ±5 V, pitch at 1 V per octave with 0 V at middle C
(261.63 Hz), gates at 0 and 10 V. The
[oscillator tutorial](../tutorials/oscillator.md#signals-are-voltages) uses
each of them.
