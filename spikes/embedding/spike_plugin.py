# SPDX-License-Identifier: GPL-3.0-or-later
"""Trusted script shipped inside the RP-04 spike plugin. Spike code, not an API."""

import sys

import _rackpy_spike as rp

rp.log(f"python {sys.version.split()[0]} prefix={sys.prefix!r} executable={sys.executable!r}")
rp.log(f"sys.path={sys.path!r}")
flags = sys.flags
rp.log(f"flags isolated={flags.isolated} no_site={flags.no_site} dont_write_bytecode={flags.dont_write_bytecode}")

for name in ("json", "decimal", "ssl", "sqlite3", "ctypes", "_dbm"):
    try:
        __import__(name)
        rp.log(f"import {name}: ok")
    except Exception as e:
        rp.log(f"import {name}: FAILED {type(e).__name__}: {e}")

v = rp.Vec(1, 2) + rp.Vec(3, 4)
rp.log(f"value {v!r} x={v.x}")

handles = []


class Child(rp.Widget):
    def on_add(self):
        rp.log("child on_add")

    def on_remove(self):
        rp.log("child on_remove")

    def __del__(self):
        rp.log("child finalized")


try:
    rp.attach(Child())
    rp.log("attach outside configure: accepted (UNEXPECTED)")
except Exception as e:
    rp.log(f"attach outside configure: {type(e).__name__}: {e}")


class GainKernel(rp.Kernel):
    def configure(self, handle):
        handles.append(handle)
        view = memoryview(handle.snapshot())
        rp.log(f"configure id={handle.id} alive={handle.alive} snapshot format={view.format} len={len(view)}")
        child = Child()
        rp.log(f"child storage inside python object={rp.storage_inside_python_object(child)}")
        try:
            rp.attach(child)
            rp.log("attach: ok")
        except Exception as e:
            rp.log(f"attach: FAILED {type(e).__name__}: {str(e).splitlines()[0]}")
        return 0.5


# The temporary is owned by C++ from here on; the Python subclass must survive.
rp.set_kernel(GainKernel())


def on_shutdown():
    for h in handles:
        try:
            h.gain  # noqa: B018 - the access is the test
            rp.log(f"stale id={h.id}: still readable (UNEXPECTED)")
        except ReferenceError as e:
            rp.log(f"stale id={h.id}: ReferenceError ({e})")
    rp.log(f"shutdown handles={len(handles)}")
