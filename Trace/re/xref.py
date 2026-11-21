import pefile,capstone,struct,sys,re
G=r"H:/Games/SteamLibrary/steamapps/common/Worms Clan Wars/WormsClanWars.exe"
pe=pefile.PE(G,fast_load=True); IB=pe.OPTIONAL_HEADER.ImageBase
data=open(G,'rb').read()
def off2va(o):
    for s in pe.sections:
        if s.PointerToRawData<=o<s.PointerToRawData+s.SizeOfRawData: return IB+s.VirtualAddress+o-s.PointerToRawData
def va2off(v):
    for s in pe.sections:
        if s.VirtualAddress<=v-IB<s.VirtualAddress+s.Misc_VirtualSize: return v-IB-s.VirtualAddress+s.PointerToRawData
text=pe.sections[0]; t0=text.PointerToRawData; tdata=data[t0:t0+text.SizeOfRawData]; tva=IB+text.VirtualAddress
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
def find_str(s):
    i=data.find(s.encode()+b'\0'); 
    while i>0 and data[i-1]!=0: i=data.find(s.encode()+b'\0',i+1)
    return off2va(i)
def xrefs(va):
    return [tva+m.start() for m in re.finditer(re.escape(struct.pack('<I',va)),tdata)]
def dis(va,before=0x30,after=0x40):
    start=va-before; o=va2off(start)
    out=[]
    for ins in md.disasm(data[o:o+before+after],start):
        out.append(f"{ins.address:08x} {ins.mnemonic} {ins.op_str}")
    return out
def func_start(va):
    # scan back for int3/ret padding then push ebp pattern
    o=va2off(va)
    while o>t0:
        if data[o-1] in (0xcc,0xc3) and data[o:o+3] in (b'\x55\x8b\xec',) : return off2va(o)
        if data[o-1]==0xcc and data[o]!=0xcc: return off2va(o)
        o-=1
if __name__=='__main__':
    for s in sys.argv[1:]:
        v=find_str(s); print('==',s,hex(v) if v else None)
        if not v: continue
        for x in xrefs(v)[:6]:
            print('-- xref',hex(x),'func',hex(func_start(x)))
            print('\n'.join(dis(x-1,0x20,0x30)))
