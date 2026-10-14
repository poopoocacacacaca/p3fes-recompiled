# Find function entry points reached only through pointers (callbacks,
# vtables, jump-through-register) that the decomp's symbol list lacks.
import struct, sys, bisect
elf, mapcsv = sys.argv[1:3]
d = open(elf, 'rb').read()
BASE, OFF = 0x100000, 0x80
TEXT_END, DATA_END = 0x5A1D4C, 0x7CDE80
def w(a): return struct.unpack_from('<I', d, a - BASE + OFF)[0]
starts = sorted(int(l.split(',')[1], 16) for l in open(mapcsv).readlines()[1:])
S = set(starts)
def boundary_ok(a):
    # previous real instruction pair is "jr ra; <delay>" with only zero padding between
    p = a - 4
    while p > BASE and w(p) == 0 and a - p <= 64: p -= 4
    # p is the last nonzero word before a; it is either the delay slot or jr ra itself (delay slot nop skipped)
    return w(p) == 0x03E00008 or w(p - 4) == 0x03E00008 or (w(p) >> 26) == 2 or (w(p - 4) >> 26) == 2
cands = {}
# (1) pointers stored in data/rodata
for a in range(0x5A9D3C, DATA_END, 4):
    v = w(a)
    if BASE <= v < TEXT_END and v % 4 == 0 and v not in S:
        cands.setdefault(v, set()).add('data')
# (2) lui/addiu or lui/ori pairs in code
for a in range(BASE, TEXT_END, 4):
    i = w(a)
    if i >> 26 == 0x0F:  # lui
        rt = (i >> 16) & 31; hi = (i & 0xFFFF) << 16
        for b in range(a + 4, min(a + 32, TEXT_END), 4):
            j = w(b); op = j >> 26
            if op in (0x09, 0x0D) and ((j >> 21) & 31) == rt:
                lo = j & 0xFFFF
                v = (hi + (lo - 0x10000 if (op == 0x09 and lo & 0x8000) else lo)) & 0xFFFFFFFF
                if BASE <= v < TEXT_END and v % 4 == 0 and v not in S:
                    cands.setdefault(v, set()).add('code')
                break
good = sorted(v for v in cands if boundary_ok(v) and w(v) != 0)
print('pointer targets not in map:', len(cands), ' at function boundaries:', len(good))
with open(sys.argv[3], 'w') as f:
    for v in good: f.write('0x%08X %s\n' % (v, ','.join(sorted(cands[v]))))
