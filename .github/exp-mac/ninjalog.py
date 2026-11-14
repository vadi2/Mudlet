#!/usr/bin/env python3
# Longest steps of one ninja invocation: build.sh saves the .ninja_log lines that invocation appended.
import sys

rows = []
for line in open(sys.argv[1]):
    if line.startswith("#"):
        continue
    f = line.rstrip("\n").split("\t")
    if len(f) >= 4:
        rows.append((int(f[1]) - int(f[0]), int(f[0]), int(f[1]), f[3]))
if not rows:
    print("no steps")
    sys.exit()
span = max(r[2] for r in rows) - min(r[1] for r in rows)
print(f"steps {len(rows)} span {span / 1000:.0f} s sum {sum(r[0] for r in rows) / 1000:.0f} s")
for d, s, e, out in sorted(rows, reverse=True)[:12]:
    print(f"{d / 1000:8.1f} s  {out}")
