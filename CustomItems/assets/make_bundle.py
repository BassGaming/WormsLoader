"""Builds CustomItems.bdl from the game's Weapons.bdl.

Each model is a copy of the cluster bomblet's node tree (shader, textures, mesh), registered under a new
resource name; mesh/texture data can be replaced per model.
Usage: python make_bundle.py <game folder> <out.bdl>
"""
import os
import struct
import sys

_here = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [_here, os.path.join(_here, '..', '..', 'tools')]
from xom import Xom, Container, Reader, LAYOUT, venc, vint  # noqa: E402
from chart import chart_mesh, dxt5_solid  # noqa: E402

SRC_MESH = '3DClusterGrenade_Bomb'


def find_resource(x, name):
    r = x.containers[x.root].walk()
    for (_, _, ci), (_, _, si) in zip(r.refs, r.strs):
        if x.strings[si] == name:
            return ci
    raise KeyError(name)


def build(game, out, models):
    """models: {resource name: callback(containers dict old_index -> Container) or None}"""
    src = Xom.load(os.path.join(game, 'DataPC', 'Bundles', 'Weapons.bdl'))
    names = src.type_names
    _, o = vint(src.containers[src.root].body, 0)
    gs_guid = src.containers[src.root].body[o:o + 16]

    items = []  # (type, key, make(body index fn, str fn) -> body bytes)
    key_of = {}  # key -> new index (filled later)
    desc_src = find_resource(src, SRC_MESH)
    desc = src.containers[desc_src]
    _, _, node_src = desc.walk().refs[2]
    tree = src.subgraph([node_src])

    master_entries = []
    for m, (name, edit) in enumerate(models.items()):
        copies = {i: src.containers[i] for i in tree}
        if edit:
            edit(copies, src)
        for i in tree:
            items.append((copies[i].type, (m, i), copies[i]))
        items.append(('XGraphSet', (m, 'gs'), None))
        items.append(('XMeshDescriptor', (m, 'desc'), None))
        master_entries.append((name, (m, 'desc')))
    items.append(('XGraphSet', 'master', None))

    # Containers are grouped by TYPE order; the master graph set goes first among the graph sets.
    items.sort(key=lambda it: (names.index(it[0]), it[1] != 'master'))
    for n, (_, k, _) in enumerate(items, 1):
        key_of[k] = n

    strings = ['']
    str_idx = {'': 0}

    def s(text):
        if text not in str_idx:
            str_idx[text] = len(strings)
            strings.append(text)
        return str_idx[text]

    def graphset(entries):
        body = venc(len(entries))
        for text, k in entries:
            body += gs_guid + venc(key_of[k]) + venc(s(text))
        return Container('XGraphSet', b'', body)

    x = Xom()
    x.header, x.type_entries, x.guid, x.schm = src.header, src.type_entries, src.guid, src.schm
    for xtype, k, c in items:
        if k == 'master':
            c = graphset(master_entries)
            x.root = key_of[k]
        elif k[1] == 'gs':
            c = graphset([('world', (k[0], node_src))])
        elif k[1] == 'desc':
            m = k[0]
            gs_old, anim_old, _ = [v for _, _, v in desc.walk().refs]
            remap = {gs_old: key_of[(m, 'gs')], anim_old: 0, node_src: key_of[(m, node_src)]}
            c = desc.remap(lambda v: remap[v], lambda v: s(master_entries[m][0]))
        else:
            m = k[0]
            c = c.remap(lambda v, m=m: key_of[(m, v)] if v else 0, lambda v: s(src.strings[v]))
        x.containers.append(c)
    x.strings = strings
    x.save(out)
    return x


def _bounds(verts):
    lo = [min(v[i] for v in verts) for i in range(3)]
    hi = [max(v[i] for v in verts) for i in range(3)]
    c = [(a + b) / 2 for a, b in zip(lo, hi)]
    rad = max(sum((v[i] - c[i]) ** 2 for i in range(3)) ** 0.5 for v in verts)
    return c, rad, lo, hi


def use_mesh(mesh, image_d, image_n):
    """Edit callback: replace geometry (mesh = dict of attribute lists + indices) and the D/N textures."""
    def edit(copies, src):
        streams = {'INvert.Position': ('3f', mesh['pos']), 'INvert.Normal': ('3f', mesh['nrm']),
                   'INvert.Binormal': ('3f', mesh['bin']), 'INvert.Tangent': ('3f', mesh['tan']),
                   'INvert.TexCoord1': ('2f', mesh['uv'])}
        c_, rad, lo, hi = _bounds(mesh['pos'])
        sphere = struct.pack('<4f', *c_, rad)
        box = struct.pack('<6f', *lo, *hi)
        for i, c in list(copies.items()):
            b = c.body
            if c.type == 'XPsVertexDataSet':
                si, o = vint(b, 0)
                size, o2 = vint(b, o)
                fmt, data = streams[src.strings[si]]
                raw = b''.join(struct.pack('<' + fmt, *v) for v in data)
                b = b[:o] + venc(len(raw)) + raw + b[o2 + size:]
            elif c.type == 'XIndexSet':
                b = venc(len(mesh['idx'])) + struct.pack('<%dH' % len(mesh['idx']), *mesh['idx'])
            elif c.type == 'XPsGeoSet':
                b = struct.pack('<I', len(mesh['idx']) // 3) + b[4:]
            elif c.type == 'XInteriorNode':  # children, sphere 16, box 24, 4, name
                r = Reader(b, 0, len(b)); r.set()
                b = b[:r.o] + sphere + box + b[r.o + 40:]
            elif c.type == 'XPsShape':  # 9, ref, ref, 5, sphere 16, box 24 (left empty), 4, name
                r = Reader(b, 0, len(b)); r.skip(9); r.ref(); r.ref(); r.skip(5)
                b = b[:r.o] + sphere + b[r.o + 16:]
            elif c.type == 'XImage' and src.strings[vint(b, 0)[0]].endswith(('_D.tga', '_N.tga')):
                si, o = vint(b, 0)
                pix = image_d if src.strings[si].endswith('_D.tga') else image_n
                hdr_end = b.index(venc(len(pix)), o)  # header up to the data size varint (same size: 256x256 DXT5)
                b = b[:hdr_end] + venc(len(pix)) + pix + b[hdr_end + len(venc(len(pix))) + len(pix):]
            copies[i] = Container(c.type, c.head, b)
    return edit


if __name__ == '__main__':
    mesh = chart_mesh()
    d = dxt5_solid(256, 256, lambda u, v: (200, 25, 25) if u < 0.5 else (70, 70, 75), alpha=0)
    n = dxt5_solid(256, 256, lambda u, v: (128, 128, 255), alpha=102)
    x = build(sys.argv[1], sys.argv[2], {'StockChart_Bomb': use_mesh(mesh, d, n)})
    print('wrote', sys.argv[2], len(x.containers) - 1, 'containers', len(x.strings), 'strings')
    y = Xom.load(sys.argv[2])  # self-check: the result parses and resolves
    print('reload ok, root', y.root, y.containers[y.root].type, 'subgraph', len(y.subgraph([y.root])))
