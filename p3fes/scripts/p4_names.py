#!/usr/bin/env python3
"""Transfer Persona 4 decomp names onto Persona 3 FES functions by aligning function-size sequences.

P3 FES and P4 share engine, middleware and SDK code linked in the same order, so runs of consecutive functions
with identical sizes mark the same code. Only runs of >= MIN_RUN identical sizes are trusted.
"""
import csv, json, re, sys, pathlib, difflib, collections

P4 = pathlib.Path(sys.argv[1])
P3MAP = pathlib.Path(sys.argv[2])
OUT = pathlib.Path(sys.argv[3])
MIN_RUN = int(sys.argv[4]) if len(sys.argv) > 4 else 4

KEYWORDS = {"if", "while", "for", "switch", "return", "sizeof", "INCLUDE_ASM", "void", "int", "char", "short", "long",
            "float", "double", "unsigned", "signed", "static", "const", "struct", "union", "enum", "extern", "inline"}
w = json.load(open(P4 / "tools/slus21782_functions.json"))["windows"]
p4 = sorted((int(a, 16), s) for a, s in w.items())
names4 = {}
for line in open(P4 / "config/symbol_addrs.txt"):
    m = re.match(r"\s*([A-Za-z_]\w*)\s*=\s*0x([0-9A-Fa-f]+);", line)
    if m and not m.group(1).startswith("func_"):
        names4[int(m.group(2), 16)] = (m.group(1), "symbol_addrs")
src_names = 0
for f in (P4 / "src").rglob("*.c"):
    lines = f.read_text(errors="replace").split("\n")
    for i, l in enumerate(lines):
        m = re.match(r"\s*//\s*FUN_([0-9A-Fa-f]{8})\b", l)
        if not m:
            continue
        for l2 in lines[i + 1:i + 8]:
            if l2.lstrip().startswith(("//", "/*", "*", "#")):
                continue
            m2 = re.search(r"([A-Za-z_]\w*)\s*\(", l2)
            if m2 and (l2[:1].isspace() or "=" in l2[:m2.start()] or m2.group(1).isupper()):
                break  # not a definition (call, macro or statement)
            if m2 and m2.group(1) not in KEYWORDS:
                a = int(m.group(1), 16)
                if not m2.group(1).startswith(("func_", "FUN_")) and a not in names4:
                    names4[a] = (m2.group(1), str(f.relative_to(P4)))
                    src_names += 1
                break
print(f"P4: {len(p4)} functions, {len(names4)} named ({src_names} from source markers)")

rows = [r for r in csv.reader(open(P3MAP)) if r[1].startswith("0x")]
p3 = sorted((int(r[1], 16), int(r[3]), r[0]) for r in rows)
s3 = [s for _, s, _ in p3]
s4 = [s for _, s in p4]
sm = difflib.SequenceMatcher(None, s3, s4, autojunk=False)
pairs = {}
run_of = {}
for b in sm.get_matching_blocks():
    if b.size >= MIN_RUN and len(set(s3[b.a:b.a + b.size])) >= 3:
        for k in range(b.size):
            if s3[b.a + k] <= 16 and len(set(s3[max(b.a, b.a + k - 1):b.a + k + 2])) == 1:
                continue  # identical tiny neighbours (setters) can be swapped
            pairs[b.a + k] = b.b + k
            run_of[b.a + k] = b.a
print(f"aligned {len(pairs)} P3 functions in runs >= {MIN_RUN}")

# Validation: P3 human names vs P4 names on aligned pairs.
human = lambda n: not re.search(r"(_[0-9a-f]{8}|[0-9a-fA-F]{6,})$", n) and not n.startswith(("sub_", "func_", "FUN_"))
human = lambda n: not re.search(r"(_[0-9a-f]{8}|[0-9a-fA-F]{6,})$", n) and not n.startswith(("sub_", "func_", "FUN_"))
norm = lambda n: re.sub(r"^_+|_+$", "", n.lower()).replace("__dtoa_", "").replace("alloc_", "malloc_")
run_state = collections.defaultdict(lambda: [0, 0])
for i3, i4 in pairs.items():
    a4 = p4[i4][0]
    if a4 in names4 and human(p3[i3][2]):
        run_state[run_of[i3]][0 if norm(p3[i3][2]) == norm(names4[a4][0]) else 1] += 1
print(f"anchors before filtering: {sum(v[0] for v in run_state.values())} agree, {sum(v[1] for v in run_state.values())} differ")
agree = disagree = 0
bad = []
out = []
for i3, i4 in pairs.items():
    ok, wrong = run_state[run_of[i3]]
    if wrong:
        continue  # a run with a contradicting anchor is not trusted at all
    conf = "anchored" if ok else "size-run"

    a3, size, n3 = p3[i3]
    a4, _ = p4[i4]
    if a4 not in names4:
        continue
    n4, ev = names4[a4]
    if human(n3):
        if norm(n3) == norm(n4):
            agree += 1
        else:
            disagree += 1
            bad.append((n3, n4))
        continue
    out.append((a3, n3, n4, a4, ev, conf))
print(f"validation on functions named in both: {agree} agree, {disagree} differ")
for x in bad[:25]:
    print("  differ:", x)
with open(OUT, "w", newline="") as fh:
    wr = csv.writer(fh)
    wr.writerow(["p3_address", "p3_map_name", "p4_name", "p4_address", "p4_evidence", "confidence"])
    for a3, n3, n4, a4, ev, conf in sorted(out):
        wr.writerow([f"0x{a3:08X}", n3, n4, f"0x{a4:08X}", ev, conf])
print(f"{sum(1 for o in out if o[5]=='anchored')} anchored, {sum(1 for o in out if o[5]!='anchored')} size-run; dropped runs: {sum(1 for v in run_state.values() if v[1])}")
print(f"{len(out)} new names for P3 functions -> {OUT}")
