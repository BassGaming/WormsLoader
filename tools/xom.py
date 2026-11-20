"""Read/write Team17 XOM files (Worms Clan Wars .bdl/.xom, magic MOIK).

Layout (from XomView by AlexBond2, github.com/AlexBond2/Worms3DTools, checked against Clan Wars files):
  header 64 B: 'MOIK', u32 flags, ..., +0x18 type count, +0x1c container count, +0x20 root container index
  TYPE entries 64 B each: 'TYPE', u32 ?, u32 count, u32 0, GUID 16, name 32  (containers come grouped in this order)
  'GUID' 16 B, 'SCHM' 16 B, 'STRS' u32 count, u32 size, u32 offsets[count], chars (string 0 = '')
  containers 1..N: most start with 'CTNR' + 3 header bytes; descriptors and XGraphSet have no tag.
Numbers inside containers: varint (7 bits per byte, low first, max 4 bytes). References are 1-based container
indices (0 = none); strings are indices into STRS.
"""
import struct

UNTAGGED = {'XGraphSet', 'XMeshDescriptor', 'XBitmapDescriptor', 'XCustomDescriptor', 'XSpriteSetDescriptor',
            'XAnimClipLibrary'}


def vint(d, o):
    v = d[o] & 0x7f
    n = 1
    while d[o + n - 1] & 0x80 and n < 4:
        v |= (d[o + n] & 0x7f) << (7 * n)
        n += 1
    return v, o + n


def venc(v):
    out = bytearray()
    while True:
        b = v & 0x7f
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


class Reader:
    """Walks one container body; records where references and string indices are."""

    def __init__(self, d, o, end):
        self.d, self.o, self.end = d, o, end
        self.refs, self.strs = [], []

    def v(self):
        x, self.o = vint(self.d, self.o)
        return x

    def ref(self):
        o = self.o
        self.refs.append((o, self.o_after(o), self.v()))

    def str(self):
        o = self.o
        self.strs.append((o, self.o_after(o), self.v()))

    def o_after(self, o):
        return vint(self.d, o)[1]

    def set(self):
        for _ in range(self.v()):
            self.ref()

    def skip(self, n):
        self.o += n

    def rest(self):
        self.o = self.end


def _graphset(r):
    for _ in range(r.v()):
        r.skip(16)  # GUID
        r.ref()
        r.str()


def _meshdesc(r):
    r.str(); r.skip(1); r.ref()  # name, ?, per-model XGraphSet
    r.skip(8); r.ref(); r.ref()  # ..., XExpandedAnimInfo (0 = none), root node


# Per-type field walkers (Clan Wars layouts). Only fields that hold references/strings matter;
# r.rest() means "nothing else to remap". Unknown types raise KeyError.
LAYOUT = {
    'XGraphSet': _graphset,
    'XMeshDescriptor': _meshdesc,
    'XInteriorNode': lambda r: (r.set(), r.skip(44), r.str()),
    'XGroup': lambda r: (r.ref(), r.skip(1), r.set(), r.skip(44), r.str()),
    'XPsShape': lambda r: (r.skip(9), r.ref(), r.ref(), r.skip(49), r.str()),
    'XPsGeoSet': lambda r: (r.skip(4), r.set(), r.ref(), r.rest()),
    'XPsVertexDataSet': lambda r: (r.str(), r.rest()),
    'XIndexSet': lambda r: r.rest(),
    'XMatrix': lambda r: r.rest(),
    'XBlendModeGL': lambda r: r.rest(),
    'XExpandedAnimInfo': lambda r: r.rest(),
    'XPsShaderInstance': lambda r: (r.set(), r.ref(), r.set(), r.set(), r.str(), r.skip(8), r.str()),
    'XPsShaderPrototype': lambda r: r.set(),
    'XPsProgReference': lambda r: (r.str(), r.rest()),
    'XPsTextureReference': lambda r: (r.str(), r.ref(), r.rest()),
    'XOglTextureMap': lambda r: (r.skip(20), r.ref(), r.rest()),
    'XImage': lambda r: (r.str(), r.rest()),  # ends with a palette ref, always 0 here
    'XUniformInstance': lambda r: r.ref(),
    'XUniformFloat': lambda r: (r.skip(4), r.str(), r.rest()),
    'XUniformVector4f': lambda r: (r.skip(16), r.str(), r.rest()),
}
for _n in ('XUniformTime', 'XUniformViewMatrix', 'XUniformViewMatrixInverse', 'XUniformWorldViewMatrix',
           'XUniformWorldViewProjectionMatr', 'XUniformLightData'):
    LAYOUT[_n] = lambda r: (r.str(), r.rest())


class Container:
    def __init__(self, xtype, head, body):
        self.type = xtype  # type name
        self.head = head   # b'CTNR' + 3 bytes, or b'' for untagged types
        self.body = body   # bytes after the head

    def walk(self):
        r = Reader(self.body, 0, len(self.body))
        LAYOUT[self.type](r)
        if r.o != len(self.body):
            raise ValueError('%s: layout ended at %d of %d' % (self.type, r.o, len(self.body)))
        return r

    def remap(self, ref_map, str_map):
        """Return a copy with references and string indices rewritten through the two functions."""
        r = self.walk()
        edits = sorted([(a, b, venc(ref_map(v))) for a, b, v in r.refs] +
                       [(a, b, venc(str_map(v))) for a, b, v in r.strs])
        out, last = bytearray(), 0
        for a, b, enc in edits:
            out += self.body[last:a] + enc
            last = b
        out += self.body[last:]
        return Container(self.type, self.head, bytes(out))


class Xom:
    def __init__(self):
        self.header = b''
        self.type_entries = []  # raw 64-byte TYPE entries (count field rewritten on save)
        self.guid = b''
        self.schm = b''
        self.strings = ['']
        self.containers = [None]  # 1-based
        self.root = 0

    @property
    def type_names(self):
        return [e[0x20:0x40].split(b'\0')[0].decode() for e in self.type_entries]

    @staticmethod
    def load(path):
        d = open(path, 'rb').read()
        x = Xom()
        assert d[:4] == b'MOIK'
        x.header = d[:0x40]
        ntypes, count, x.root = struct.unpack_from('<III', d, 0x18)
        x.type_entries = [d[0x40 + i * 0x40:0x80 + i * 0x40] for i in range(ntypes)]
        o = 0x40 + ntypes * 0x40
        x.guid, x.schm = d[o:o + 16], d[o + 16:o + 32]
        assert d[o:o + 4] == b'GUID' and d[o + 16:o + 20] == b'SCHM' and d[o + 32:o + 36] == b'STRS'
        o += 36
        nstr, size = struct.unpack_from('<II', d, o)
        o += 8
        offs = struct.unpack_from('<%dI' % nstr, d, o)
        chars = d[o + 4 * nstr:o + 4 * nstr + size]
        x.strings = [chars[a:chars.index(b'\0', a)].decode('utf8') for a in offs]
        o += 4 * nstr + size
        seq = [n for e, n in zip(x.type_entries, x.type_names) for _ in range(struct.unpack_from('<I', e, 8)[0])]
        assert len(seq) == count
        x.containers += x._split(d, o, seq)
        return x

    @staticmethod
    def _split(d, o, seq):
        """Cut the data into containers. Untagged ones are cut by parsing; runs of untagged types we
        can't parse are kept as None (fine as long as nothing we copy points at them)."""
        tags = []
        i = d.find(b'CTNR', o)
        while i >= 0:
            tags.append(i)
            i = d.find(b'CTNR', i + 4)
        tags.append(len(d))
        assert tags[0] == o and len(tags) - 1 == sum(1 for n in seq if n not in UNTAGGED)
        out, t, k = [], 0, 0
        while k < len(seq):
            bs, be = tags[t], tags[t + 1]
            t += 1
            run = []
            j = k + 1
            while j < len(seq) and seq[j] in UNTAGGED:
                run.append(seq[j])
                j += 1
            cut = None
            if run and all(n in LAYOUT for n in run):
                for st in range(bs + 7, min(be, bs + 65536)):  # find where the untagged run starts
                    try:
                        p, ends = st, []
                        for n in run:
                            r = Reader(d, p, be)
                            LAYOUT[n](r)
                            for _, _, v in r.refs:
                                if v > len(seq): raise ValueError
                            p = r.o
                            ends.append(p)
                        if p == be:
                            cut = (st, ends)
                            break
                    except (ValueError, IndexError):
                        pass
            if cut:
                st, ends = cut
                out.append(Container(seq[k], d[bs:bs + 7], d[bs + 7:st]))
                prev = st
                for n, e in zip(run, ends):
                    out.append(Container(n, b'', d[prev:e]))
                    prev = e
            else:
                out.append(Container(seq[k], d[bs:bs + 7], d[bs + 7:be] if not run else None))
                out += [None] * len(run)
            k = j
        return out

    def save(self, path):
        names = self.type_names
        counts = [0] * len(names)
        for c in self.containers[1:]:
            counts[names.index(c.type)] += 1
        order = [names.index(c.type) for c in self.containers[1:]]
        assert order == sorted(order), 'containers must be grouped in TYPE order'
        enc = [s.encode('utf8') + b'\0' for s in self.strings]
        offs, a = [], 0
        for e in enc:
            offs.append(a)
            a += len(e)
        out = bytearray(self.header)
        struct.pack_into('<III', out, 0x18, len(names), len(self.containers) - 1, self.root)
        for e, n in zip(self.type_entries, counts):
            e = bytearray(e)
            struct.pack_into('<I', e, 8, n)
            out += e
        out += self.guid + self.schm
        out += b'STRS' + struct.pack('<II', len(enc), a) + struct.pack('<%dI' % len(offs), *offs) + b''.join(enc)
        for c in self.containers[1:]:
            out += c.head + c.body
        open(path, 'wb').write(out)

    def subgraph(self, roots):
        """All containers reachable from `roots` (1-based indices), in index order."""
        seen, todo = set(), list(roots)
        while todo:
            i = todo.pop()
            if i == 0 or i in seen:
                continue
            seen.add(i)
            todo += [v for _, _, v in self.containers[i].walk().refs]
        return sorted(seen)


if __name__ == '__main__':
    import sys
    x = Xom.load(sys.argv[1])
    print(len(x.containers) - 1, 'containers,', len(x.strings), 'strings, root', x.root, x.containers[x.root].type)
    r = x.containers[x.root].walk()
    for (_, _, ci), (_, _, si) in zip(r.refs, r.strs):
        print(' ', x.strings[si], '->', ci, x.containers[ci].type if x.containers[ci] else '?')
