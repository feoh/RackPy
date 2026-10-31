# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack output=buffers.out
from rackpy.engine import Module, Output
from rackpy.simd import float_4

out = Output()  # a port you create yourself is Python's, like any object
out.set_channels(5)
for c in range(5):
    out.set_voltage(c * 1.5, c)

view = out.voltages  # a checked view of the port's 16 voltages: reads and writes go to the port
print(len(view), view[2], view[-12])

buffer = memoryview(out.voltages)  # zero-copy floats, because Python owns the port; it keeps the port alive
out.set_voltage(-1.0, 0)
print(buffer[0], buffer.format, buffer.nbytes)  # the buffer is the port's own memory

copy = out.voltages.snapshot()  # an independent array of floats
out.set_voltage(9.0, 1)
print(copy[1], out.get_voltage(1))

print(float_4.load(buffer[1:]).tolist())  # channels 1 to 4, as from &voltages[1] in C++
print(out.get_voltage_simd(0).tolist())  # channels 0 to 3
buffer.release()

# A module's ports belong to the module, which Rack may delete: no raw buffer, but a snapshot.
module = Module.create(lambda m: m.config(0, 0, 1, 0))
module.outputs[0].set_voltage(2.5)
try:
    memoryview(module.outputs[0].voltages)
except BufferError as e:
    print("BufferError:", e.__cause__)
print(module.outputs[0].voltages.snapshot()[0])
module.close()
