#!/usr/bin/env python3
"""Find functions in the generated code whose last instruction falls through past the function's end.

usage: check_fallthrough.py <generated .cpp folder> [p3fes_map.csv]

A function whose last branch has its delay slot in the next map entry falls through to end+4, the middle of the
next function, where no entry exists: the guest thread then stops ("missing-target"). Each one needs its range
extended over the delay slot and a continuation entry at the fall-through address (see p3fes_map.csv rows named
*_cont_XXXXXXXX). Prints the offending functions and exits 1 if there are any.
"""
import csv
import os
import re
import sys

folder = sys.argv[1]
map_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "p3fes_map.csv")
starts = {int(r[1], 16) for r in list(csv.reader(open(map_path)))[1:]}
bad = 0
for name in sorted(os.listdir(folder)):
    if not name.endswith(".cpp"):
        continue
    text = open(os.path.join(folder, name), errors="ignore").read()
    m = re.search(r"// Address: 0x([0-9a-fA-F]+) - 0x([0-9a-fA-F]+)", text)
    tail = re.search(r"ctx->pc = 0x([0-9a-fA-F]+)u;\s*\}\s*$", text.rstrip())
    if not m or not tail:
        continue
    end, last = int(m.group(2), 16), int(tail.group(1), 16)
    if last != end and last not in starts:
        print(f"{name}: falls through to 0x{last:x} (function ends at 0x{end:x}), no entry there")
        bad += 1
print(f"{bad} function(s) fall through into the middle of another")
sys.exit(1 if bad else 0)
