# Resolve a base-register global at a site (by scanning back for LUI into that
# register), then list all stores/loads to that absolute address in the ELF.
import struct, sys, bisect
elf, mapcsv, site, reg, off = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4]), int(sys.argv[5])
d = open(elf, 'rb').read()
BASE, OFF, TEXT_END = 0x100000, 0x80, 0x5A1D4C
def w(a): return struct.unpack_from('<I', d, a - BASE + OFF)[0]
rows = [(int(l.split(',')[1], 16), l.split(',')[0]) for l in open(mapcsv).readlines()[1:]]
st = [r[0] for r in rows]
def fn(a):
    i = bisect.bisect_right(st, a) - 1
    return rows[i][1] if i >= 0 else '?'
# scan back within the function for lui reg
start = rows[bisect.bisect_right(st, site) - 1][0]
hi = None
for a in range(site, start - 4, -4):
    i = w(a)
    if i >> 26 == 0x0F and ((i >> 16) & 31) == reg:
        hi = (i & 0xFFFF) << 16; print('lui for reg %d at %s -> base 0x%08X' % (reg, hex(a), hi)); break
if hi is None:
    print('no lui found for that register in', fn(site)); sys.exit()
G = (hi + off) & 0xFFFFFFFF
print('global address: 0x%08X' % G)
if 0x100000 <= G < 0x100000 + 0x6CDE80:
    print('initial value in ELF: 0x%08X' % w(G))
else:
    print('initial value: in zero-filled area (0)')
STORE = {0x2B: 'sw', 0x3F: 'sd', 0x1F: 'sq'}; LOAD = {0x23: 'lw', 0x37: 'ld', 0x1E: 'lq'}
# find lui X followed by store/load with matching hi/lo (per function, track lui per register)
hits = []
for a in range(BASE, TEXT_END, 4):
    i = w(a)
    op = i >> 26
    if op in STORE or op in LOAD:
        rs = (i >> 21) & 31; imm = i & 0xFFFF; simm = imm - 0x10000 if imm & 0x8000 else imm
        # look back up to 64 instructions for lui rs
        for b in range(a - 4, max(BASE, a - 256), -4):
            j = w(b)
            if j >> 26 == 0x0F and ((j >> 16) & 31) == rs:
                if ((((j & 0xFFFF) << 16) + simm) & 0xFFFFFFFF) == G:
                    hits.append((hex(a), STORE.get(op) or LOAD.get(op), fn(a)))
                break
for h in hits: print('  ', *h)
