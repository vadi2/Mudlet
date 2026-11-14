#!/usr/bin/env python3
# Address of each hot symbol in every layout's link map, and how far it moved from the first
import re, sys

maps = []
for path in sys.argv[1:]:
    syms = {}
    for line in open(path):
        m = re.match(r"^(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)\s+\[\s*\d+\]\s+(\S+)", line)
        if m:
            syms.setdefault(m.group(3), int(m.group(1), 16))
    maps.append(syms)
common = [s for s in maps[0] if all(s in m for m in maps[1:])]
print("symbol " + " ".join(f"L{i + 1}" for i in range(len(maps))) + "  delta(mod64)")
moved = 0
for s in common:
    a = [m[s] for m in maps]
    d = [x - a[0] for x in a[1:]]
    moved += any(d)
    print(f"{s[:60]} " + " ".join(hex(x) for x in a) + "  " + " ".join(f"{x:+d}({x % 64})" for x in d))
print(f"hot symbols in every map {len(common)}, moved {moved}")
