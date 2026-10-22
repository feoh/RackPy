# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack output=rack_runtime.out
import sys

import rackpy

print("rackpy", rackpy.__version__)
print("Python", ".".join(map(str, sys.version_info[:3])))
print("isolated:", bool(sys.flags.isolated))
