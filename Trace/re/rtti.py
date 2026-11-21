# Map MSVC RTTI: class name -> vtable VA (via Complete Object Locators).
exec(open('xref.py').read().split('if __name__')[0])
import json
IBv=IB
def u32(o): return struct.unpack_from('<I',data,o)[0]
names={}
for m in re.finditer(rb'\.\?AV([A-Za-z0-9_@?$]+)@@\0',data):
    td=off2va(m.start()-8)  # TypeDescriptor: pVFTable, spare, name
    if td: names[td]=m.group(1).decode()
# COL: sig=0, offset, cdOffset, pTypeDescriptor, pClassHierarchy
cols={}
for o in range(0,len(data)-20,4):
    if u32(o)==0 and u32(o+12) in names:
        cols[off2va(o)]=(names[u32(o+12)],u32(o+4))
out={}
for colva,(n,off) in cols.items():
    for m in re.finditer(re.escape(struct.pack('<I',colva)),data):
        vt=off2va(m.start()+4)
        if vt and off==0: out.setdefault(n,[]).append(hex(vt))
json.dump(out,open('vtables.json','w'),indent=0)
print(len(names),len(cols),len(out))
