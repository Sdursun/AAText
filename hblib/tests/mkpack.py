#!/usr/bin/env python3
"""
Pack font files into one file for hbfuzz (tests/hbfuzz.c):

  mkpack.py <out> <file or directory>...

Format (big-endian): "HBFZ", count; then per font: name length, data
length, name, data.
"""
import os
import struct
import sys

files = []
for arg in sys.argv[2:]:
    if os.path.isdir(arg):
        files += sorted(os.path.join(arg, f) for f in os.listdir(arg)
                        if os.path.isfile(os.path.join(arg, f)))
    else:
        files.append(arg)

with open(sys.argv[1], "wb") as out:
    out.write(b"HBFZ" + struct.pack(">I", len(files)))
    for path in files:
        name = os.path.basename(path).encode("latin-1", "replace")[:255]
        data = open(path, "rb").read()
        out.write(struct.pack(">II", len(name), len(data)) + name + data)
print(len(files), "fonts packed into", sys.argv[1])
