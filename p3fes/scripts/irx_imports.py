import sys, struct
def dump(path):
    d = open(path,'rb').read()
    out=[]
    i=0
    while i+20 <= len(d):
        w = struct.unpack_from('<I', d, i)[0]
        if w == 0x41e00000:
            ver = struct.unpack_from('<I', d, i+8)[0]
            name = d[i+12:i+20].split(b'\0')[0].decode('latin1')
            j = i+20; ords=[]
            while j+8 <= len(d):
                a,b = struct.unpack_from('<II', d, j)
                if a == 0x03e00008 and (b>>16)==0x2400:
                    ords.append((b & 0xffff, j)); j+=8
                else: break
            out.append((name, hex(ver), ords))
            i = j
        else:
            i += 4
    return out
for p in sys.argv[1:]:
    print(p)
    for n,v,o in dump(p): print('  ',n,v,' '.join('%d@%x'%(a,b) for a,b in o))
