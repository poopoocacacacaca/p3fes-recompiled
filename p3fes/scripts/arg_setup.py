# For each call site, report how argument registers a0-a3 are set in the
# instructions preceding a JAL (register-level summary only).
import struct, sys, bisect
elf, mapcsv = sys.argv[1:3]
sites = [int(x, 16) for x in sys.argv[3:]]
d = open(elf, 'rb').read()
def w(a): return struct.unpack_from('<I', d, a - 0x100000 + 0x80)[0]
rows = [(int(l.split(',')[1], 16), l.split(',')[0]) for l in open(mapcsv).readlines()[1:]]
st = [r[0] for r in rows]
def fn(a):
    i = bisect.bisect_right(st, a) - 1
    return rows[i][1] if i >= 0 else '?'
R = ['zero','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7',
     's0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']
def desc(i):
    op = i >> 26; rs = (i >> 21) & 31; rt = (i >> 16) & 31; rd = (i >> 11) & 31
    imm = i & 0xFFFF; simm = imm - 0x10000 if imm & 0x8000 else imm
    if op == 0x0F: return rt, 'lui 0x%X' % imm
    if op in (0x09, 0x19): return rt, '%s + %d' % (R[rs], simm)
    if op == 0x0D: return rt, '%s | 0x%X' % (R[rs], imm)
    if op in (0x23, 0x37, 0x1E): return rt, 'load %d(%s)' % (simm, R[rs])
    if op == 0 and (i & 63) in (0x21, 0x25, 0x2D): return rd, '%s, %s' % (R[rs], R[rt])
    return None, None
for s in sites:
    print('call site', hex(s), 'in', fn(s))
    for a in range(s - 40, s + 8, 4):
        reg, what = desc(w(a))
        if reg in (4, 5, 6, 7):
            print('   %s  %s <- %s%s' % (hex(a), R[reg], what, '  (delay slot)' if a == s + 4 else ''))
