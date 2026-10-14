# Build a PS2Recomp function map (Name,Start,End,Size) for SLUS_216.21
# from the P3 FES decomp's symbol_addrs.txt plus JAL-target scanning.
import re, struct, sys
elf, syms_path, out = sys.argv[1:4]
extra_path = sys.argv[4] if len(sys.argv) > 4 else None
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
import bisect
with open(out, 'w') as f:
    f.write('Name,Start,End,Size\n')
    for a in s:
        j = bisect.bisect_right(bounds, a)
        e = bounds[j] if j < len(bounds) else TEXT_END
        n = names.get(a, 'sub_%08x' % a)
        n = re.sub(r'[^A-Za-z0-9_]', '_', n)
        f.write('%s,0x%08X,0x%08X,%d\n' % (n, a, e, e - a))
print('decomp funcs', len(names), 'jal targets', len(jal), 'added from jal', len(new), 'total', len(s))
