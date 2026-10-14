# Summarize control-flow/stack shape of R5900 code (no full disassembly):
# reports sp adjustments, ra saves/loads, calls, jumps and returns.
import struct, sys, bisect
elf, mapcsv = sys.argv[1:3]
ranges = [(int(x, 16), int(y, 16)) for x, y in (a.split('-') for a in sys.argv[3:])]
d = open(elf, 'rb').read()
def w(a): return struct.unpack_from('<I', d, a - 0x100000 + 0x80)[0]
rows = [(int(l.split(',')[1], 16), l.split(',')[0]) for l in open(mapcsv).readlines()[1:]]
st = [r[0] for r in rows]
names = dict((a, n) for a, n in rows)
def fn(a):
    i = bisect.bisect_right(st, a) - 1
    return rows[i][1] if i >= 0 else '?'
R = {29: 'sp', 31: 'ra', 30: 'fp', 0: 'zero', 2: 'v0', 4: 'a0'}
def r(n): return R.get(n, 'r%d' % n)
for lo, hi in ranges:
    print('--- %s..%s' % (hex(lo), hex(hi)))
    for a in range(lo, hi, 4):
        i = w(a); op = i >> 26; rs = (i >> 21) & 31; rt = (i >> 16) & 31; imm = i & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        tag = ''
        if a in names: tag = '<' + names[a] + '> '
        s = None
        if op == 0x09 and rt == 29: s = 'addiu sp, %s, %d' % (r(rs), simm)
        elif op == 0x19 and rt == 29: s = 'daddiu sp, %s, %d' % (r(rs), simm)
        elif op in (0x2B, 0x3F, 0x1F) and rt == 31: s = 'store ra -> %d(%s)' % (simm, r(rs))
        elif op in (0x23, 0x37, 0x1E) and rt == 31: s = 'load ra <- %d(%s)' % (simm, r(rs))
        elif op in (0x23, 0x37, 0x1E) and rt == 29: s = 'LOAD sp <- %d(%s)' % (simm, r(rs))
        elif op == 0x0F and rt == 29: s = 'LUI sp'
        elif op == 0 and (i & 63) in (0x21, 0x2D, 0x25) and ((i >> 11) & 31) == 29: s = 'MOVE sp <- %s/%s' % (r(rs), r(rt))
        elif op == 3: t = ((i & 0x3FFFFFF) << 2) | ((a + 4) & 0xF0000000); s = 'jal %s %s' % (hex(t), fn(t))
        elif op == 2: t = ((i & 0x3FFFFFF) << 2) | ((a + 4) & 0xF0000000); s = 'j %s %s' % (hex(t), fn(t))
        elif op == 0 and (i & 63) == 8: s = 'jr %s' % r(rs)
        elif op == 0 and (i & 63) == 9: s = 'jalr %s' % r(rs)
        if s or tag: print('  %s %s%s' % (hex(a), tag, s or ''))
