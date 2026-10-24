# SPDX-License-Identifier: GPL-3.0-or-later
# Stub for rackpy.raw.asset (native/bindings/plugin.cpp), RP-16B.
from rackpy.raw.plugin import Plugin

systemDir: str
userDir: str
oldUserDir: str
bundlePath: str

def system(filename: str = "") -> str: ...
def user(filename: str = "") -> str: ...
def plugin(plugin: Plugin, filename: str = "") -> str: ...
