#!/usr/bin/env python3
"""Summarise a PS2X_PROFILE sample file per thread and per function.

usage: prof_report.py <samples> [thread-name-filter] [top N]
Reads <samples>.maps to resolve PCs in the executable and shared libraries.
"""
import bisect
import collections
import subprocess
import sys

samples_path = sys.argv[1]
thread_filter = sys.argv[2] if len(sys.argv) > 2 else ""
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40

# Executable mappings: (start, end, file offset, path)
regions = []
for line in open(samples_path + ".maps"):
    parts = line.split()
    if len(parts) < 6 or "x" not in parts[1]:
        continue
    lo, hi = (int(x, 16) for x in parts[0].split("-"))
    regions.append((lo, hi, int(parts[2], 16), parts[5]))
regions.sort()
bases = {}
for line in open(samples_path + ".maps"):
    parts = line.split()
    if len(parts) >= 6 and int(parts[2], 16) == 0 and parts[5] not in bases:
        bases[parts[5]] = int(parts[0].split("-")[0], 16)

symtabs = {}


def symtab(path):
    if path in symtabs:
        return symtabs[path]
    syms = []
    for flags in (["--defined-only"], ["-D", "--defined-only"]):
        out = subprocess.run(["nm", "-C", "-n"] + flags + [path], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split(" ", 2)
            if len(parts) == 3 and parts[1] in "tTwWiu":
                syms.append((int(parts[0], 16), parts[2]))
        if syms:
            break
    syms.sort()
    symtabs[path] = ([a for a, _ in syms], [n for _, n in syms])
    return symtabs[path]


def name(pc):
    i = bisect.bisect_right(regions, (pc, float("inf"))) - 1
    if i < 0 or not (regions[i][0] <= pc < regions[i][1]):
        return "[unknown]"
    path = regions[i][3]
    rel = pc - bases.get(path, regions[i][0] - regions[i][2])
    addrs, names = symtab(path)
    j = bisect.bisect_right(addrs, rel) - 1
    lib = path.rsplit("/", 1)[-1]
    sym = names[j] if j >= 0 else "?"
    return sym if lib.startswith("ps2EntryRunner") else f"{sym} [{lib}]"


per_thread = collections.Counter()
per_func = collections.Counter()
total = 0
for line in open(samples_path):
    tid, comm, pc, hits = line.split()
    hits = int(hits)
    per_thread[comm] += hits
    if thread_filter and thread_filter not in comm:
        continue
    per_func[name(int(pc, 16))] += hits
    total += hits

print("threads:", ", ".join(f"{k}={v}" for k, v in per_thread.most_common()))
print(f"samples in filter '{thread_filter}': {total}")
for func, hits in per_func.most_common(top):
    print(f"{100.0 * hits / max(total, 1):6.2f}%  {hits:7d}  {func[:150]}")
