# All direct call targets in .text + all vtable entries -> targets.txt (hex VAs)
exec(open('xref.py').read().split('if __name__')[0])
import json
tend=tva+len(tdata)
T=set()
for m in re.finditer(rb'\xe8',tdata):
    i=m.start()
    if i+5>len(tdata): continue
    tgt=tva+i+5+struct.unpack_from('<i',tdata,i+1)[0]
    if tva<=tgt<tend: T.add(tgt)
vt=json.load(open('vtables.json'))
for n,vs in vt.items():
    for v in vs:
        o=va2off(int(v,16))
        for k in range(200):
            f=struct.unpack_from('<I',data,o+4*k)[0]
            if not (tva<=f<tend): break
            T.add(f)
# keep targets that look like function starts: previous byte is int3/ret/nop-ish padding, or preceded by ret
good=[]
for t in sorted(T):
    o=va2off(t); p=data[o-1]
    if p in (0xcc,0xc3,0x90) or data[o-3]==0xc2: good.append(t)
open('targets.txt','w').write('\n'.join('%x'%t for t in good))
print(len(T),len(good))
