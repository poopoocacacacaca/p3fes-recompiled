# Build a PS2Recomp function map (Name,Start,End,Size) for SLUS_216.21
# from the P3 FES decomp's symbol_addrs.txt plus JAL-target scanning.
#   make_map.py SLUS_216.21 symbol_addrs.txt out.csv [extra_starts.txt] [names/p4_names.csv]
# The optional names overlay (p3fes/scripts/p4_names.py) replaces address-suffixed placeholder names with names
# transferred from the Persona 4 decomp; only rows marked "anchored" are used.
import csv, re, struct, sys
elf, syms_path, out = sys.argv[1:4]
extra_path = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] != '-' else None
overlay_path = sys.argv[5] if len(sys.argv) > 5 else None
d = open(elf, 'rb').read()
BASE, OFF = 0x100000, 0x80
TEXT_END = 0x100000 + 0x4A9D3C
def w(a): return struct.unpack_from('<I', d, a - BASE + OFF)[0]
names = {}
for line in open(syms_path):
    m = re.match(r'\s*(\S+)\s*=\s*0x([0-9A-Fa-f]+);\s*//.*type:func', line)
    if m:
        a = int(m[2], 16)
        if BASE <= a < TEXT_END:
            names[a] = m[1]
starts = set(names)
jal = set()
for a in range(BASE, TEXT_END, 4):
    ins = w(a)
    if ins >> 26 == 3:  # JAL
        t = ((ins & 0x3FFFFFF) << 2) | ((a + 4) & 0xF0000000)
        if BASE <= t < TEXT_END:
            jal.add(t)
new = jal - starts
starts |= jal
if extra_path:
    for line in open(extra_path):
        if line.strip(): starts.add(int(line.split()[0], 16))
# Known data blocks inside the text segment (no code; decoding them as
# instructions produces garbage). 0x5A1D4C..TEXT_END: CRI Sofdec tables.
DATA_RANGES = [(0x5A1D4C, TEXT_END)]
def in_data(a): return any(lo <= a < hi for lo, hi in DATA_RANGES)
s = sorted(a for a in starts if not in_data(a))
bounds = sorted(set(s) | {lo for lo, hi in DATA_RANGES})
overlay = {}
if overlay_path:
    taken = set(names.values())
    # Names the runtime binds to HLE implementations (ps2_call_list.h X(...) entries) would change behaviour,
    # not just the name, so the overlay never introduces them.
    import pathlib
    call_list = pathlib.Path(__file__).resolve().parents[2] / 'ps2xRuntime' / 'include' / 'ps2_call_list.h'
    reserved = set(re.findall(r'X\(\s*([A-Za-z_]\w*)\s*\)', call_list.read_text())) if call_list.exists() else set()
    for row in csv.DictReader(open(overlay_path)):
        pn = row['p4_name']
        # The runtime also matches a stub name with one leading underscore added or removed.
        if row['confidence'] != 'anchored' or pn in reserved or '_' + pn in reserved or (pn.startswith('_') and pn[1:] in reserved):
            continue
        a = int(row['p3_address'], 16)
        n = row['p4_name'] if row['p4_name'] not in taken else '%s_%08x' % (row['p4_name'], a)
        overlay[a] = n
        taken.add(n)
import bisect
with open(out, 'w') as f:
    f.write('Name,Start,End,Size\n')
    for a in s:
        j = bisect.bisect_right(bounds, a)
        e = bounds[j] if j < len(bounds) else TEXT_END
        n = names.get(a, 'sub_%08x' % a)
        if a in overlay and (n.startswith('sub_') or re.search(r'_[0-9a-f]{8}$', n)):
            n = overlay[a]
        n = re.sub(r'[^A-Za-z0-9_]', '_', n)
        f.write('%s,0x%08X,0x%08X,%d\n' % (n, a, e, e - a))
print('decomp funcs', len(names), 'jal targets', len(jal), 'added from jal', len(new), 'total', len(s))
