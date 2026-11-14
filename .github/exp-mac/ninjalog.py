#!/usr/bin/env python3
# Longest steps and every slow link in a .ninja_log, split into one section per ninja invocation
# (times restart near zero with each one). usage: ninjalog.py <.ninja_log>
import sys

segs, cur, top = [], [], 0
for line in open(sys.argv[1]):
    if line.startswith("#"):
        continue
    f = line.rstrip("\n").split("\t")
    if len(f) < 4:
        continue
    s, e, out = int(f[0]), int(f[1]), f[3]
    if cur and e < top * 0.5 and top > 10000:
        segs.append(cur)
        cur, top = [], 0
    cur.append((e - s, s, e, out))
    top = max(top, e)
if cur:
    segs.append(cur)
for i, rows in enumerate(segs, 1):
    span = max(r[2] for r in rows) - min(r[1] for r in rows)
    print(f"== invocation {i}: steps {len(rows)} span {span / 1000:.0f} s, cpu-sum {sum(r[0] for r in rows) / 1000:.0f} s")
    for d, s, e, out in sorted(rows, reverse=True)[:8]:
        print(f"{d / 1000:8.1f} s  {out}")
    links = [r for r in rows if not r[3].endswith((".o", ".gch", ".pch", ".h", ".cpp", ".stamp", ".json", ".qm")) and r[0] > 20000]
    if links:
        print(f"  slow non-object steps ({len(links)}), by end time:")
        for d, s, e, out in sorted(links, key=lambda r: r[2]):
            print(f"  {s / 1000:7.0f}-{e / 1000:7.0f} s {d / 1000:7.1f} s  {out}")
