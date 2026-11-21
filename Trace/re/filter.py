# Drop targets whose own code branches back into their first 5 bytes (the hook's jmp overwrites them).
exec(open('xref.py').read().split('if __name__')[0])
T=[int(l,16) for l in open('targets.txt')]
keep=[];bad=0
for i,t in enumerate(T):
    end=min(T[i+1] if i+1<len(T) else t+0x2000, t+0x4000)
    o=va2off(t); hit=False
    for ins in md.disasm(data[o:o+(end-t)],t):
        if ins.mnemonic.startswith('j') or ins.mnemonic.startswith('loop'):
            try: d=int(ins.op_str,16)
            except: continue
            if t<d<t+5: hit=True;break
    if hit: bad+=1
    else: keep.append(t)
open('targets.txt','w').write('\n'.join('%x'%t for t in keep))
print('dropped',bad,'kept',len(keep), '0x407960 kept?',0x407960 in keep)
