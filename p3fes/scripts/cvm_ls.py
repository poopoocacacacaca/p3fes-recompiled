import struct,sys
path=sys.argv[1]; base_lsn=int(sys.argv[2],16); targets=[int(x,16) for x in sys.argv[3:]]
f=open(path,'rb')
# find ISO PVD "CD001"
data=f.read(0x20000)
off=data.find(b'\x01CD001')
iso0=off-0x8000  # byte offset of ISO sector 0 inside CVM
pvd=data[off:off+2048]
root=pvd[156:156+34]
def rd(lsn,n):
    f.seek(iso0+lsn*2048); return f.read(n)
files=[]
def walk(lsn,size,prefix):
    d=rd(lsn,size); i=0
    while i<len(d):
        l=d[i]
        if l==0:
            i=((i//2048)+1)*2048; continue
        e=d[i:i+l]; elsn=struct.unpack_from('<I',e,2)[0]; esz=struct.unpack_from('<I',e,10)[0]
        fl=e[25]; nl=e[32]; name=e[33:33+nl]
        if name not in (b'\x00',b'\x01'):
            n=prefix+'/'+name.decode('latin1')
            if fl&2: walk(elsn,esz,n)
            else: files.append((elsn,esz,n))
        i+=l
walk(struct.unpack_from('<I',root,2)[0],struct.unpack_from('<I',root,10)[0],'')
abs0=base_lsn+iso0//2048
print('iso0 sectors',iso0//2048,'files',len(files))
for t in targets:
    for lsn,sz,n in files:
        a=abs0+lsn
        if a<=t<a+(sz+2047)//2048: print(hex(t),n,hex(sz))
if not targets:
    for lsn,sz,n in files:
        if 'MOV' in n.upper() or n.upper().endswith('.SFD') or n.upper().endswith('.PSS') or n.upper().endswith('.PMSF'): print(n,hex(sz))
