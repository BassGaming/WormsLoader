"""The stock-crash fragment: a red falling chart line with an arrowhead over grey axes, as a flat-shaded
3D mesh (no transparency needed). Texture: u < 0.5 = red, u >= 0.5 = grey (see make_bundle.py)."""
import math
import struct

RED, GREY = (0.25, 0.5), (0.75, 0.5)  # uv of the two colours


def prism(poly, z0, z1, uv, out):
    """Convex polygon (CCW in x/y) extruded from z0 to z1, flat normals, CCW-outward triangles."""
    def face(pts, n):
        t = [pts[1][i] - pts[0][i] for i in range(3)]
        ln = math.sqrt(sum(c * c for c in t)) or 1
        t = [c / ln for c in t]
        bn = [n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0]]
        base = len(out['pos'])
        for p in pts:
            out['pos'].append(p); out['nrm'].append(n); out['tan'].append(t); out['bin'].append(bn)
            out['uv'].append(uv)
        for k in range(1, len(pts) - 1):
            out['idx'] += [base, base + k, base + k + 1]

    face([(x, y, z1) for x, y in poly], (0, 0, 1))              # front (+z)
    face([(x, y, z0) for x, y in reversed(poly)], (0, 0, -1))   # back
    for a, b in zip(poly, poly[1:] + poly[:1]):                  # sides
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dy)
        face([(a[0], a[1], z0), (b[0], b[1], z0), (b[0], b[1], z1), (a[0], a[1], z1)], (dy / ln, -dx / ln, 0))


def segment(a, b, w):
    dx, dy = b[0] - a[0], b[1] - a[1]
    ln = math.hypot(dx, dy)
    nx, ny = -dy / ln * w / 2, dx / ln * w / 2
    return [(a[0] - nx, a[1] - ny), (b[0] - nx, b[1] - ny), (b[0] + nx, b[1] + ny), (a[0] + nx, a[1] + ny)]


def chart_mesh(scale=1.0):
    m = {k: [] for k in ('pos', 'nrm', 'bin', 'tan', 'uv', 'idx')}
    # axes: vertical and horizontal bar
    prism([(-2.0, -1.6), (-1.7, -1.6), (-1.7, 2.0), (-2.0, 2.0)], -0.2, 0.2, GREY, m)
    prism([(-2.0, -1.9), (2.2, -1.9), (2.2, -1.6), (-2.0, -1.6)], -0.2, 0.2, GREY, m)
    # falling line
    pts = [(-1.5, 1.6), (-0.7, 0.3), (0.0, 0.9), (0.9, -0.6)]
    for a, b in zip(pts, pts[1:]):
        prism(segment(a, b, 0.32), -0.3, 0.3, RED, m)
    # arrowhead at the end, along the last segment
    (ax, ay), (bx, by) = pts[-2], pts[-1]
    ln = math.hypot(bx - ax, by - ay)
    dx, dy = (bx - ax) / ln, (by - ay) / ln
    tip = (bx + dx * 0.7, by + dy * 0.7)
    l = (bx - dy * 0.45, by + dx * 0.45)
    r = (bx + dy * 0.45, by - dx * 0.45)
    prism([r, tip, l], -0.3, 0.3, RED, m)
    # centre on the bounding box so it spins around its middle
    xs = [p[0] for p in m['pos']]; ys = [p[1] for p in m['pos']]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    m['pos'] = [((x - cx) * scale, (y - cy) * scale, z * scale) for x, y, z in m['pos']]
    # Projectiles fly along model +Z and the camera looks along model X (flat projectiles such as
    # 3DPlugHolePayload are thin in X), from -X: rotate +90 deg about Y (chart right -> -Z, front -> +X).
    # Verified in game; the -90 deg version showed the chart mirrored.
    rot = lambda v: (v[2], v[1], -v[0])
    for k in ('pos', 'nrm', 'bin', 'tan'):
        m[k] = [rot(v) for v in m[k]]
    assert len(m['pos']) < 65536 and len(m['idx']) % 3 == 0
    return m


def dxt5_solid(w, h, color_at, alpha):
    """DXT5 image where every 4x4 block is one colour: color_at(u, v) -> (r, g, b) at the block centre."""
    out = bytearray()
    for by in range(h // 4):
        for bx in range(w // 4):
            r, g, b = color_at((bx * 4 + 2) / w, (by * 4 + 2) / h)
            c = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)
            out += bytes([alpha, alpha]) + bytes(6) + struct.pack('<HH', c, c) + bytes(4)
    return bytes(out)


if __name__ == '__main__':  # self-check: closed, CCW-outward mesh
    m = chart_mesh()
    bad = 0
    for t in range(0, len(m['idx']), 3):
        a, b, c = (m['pos'][i] for i in m['idx'][t:t + 3])
        u = [b[i] - a[i] for i in range(3)]; v = [c[i] - a[i] for i in range(3)]
        g = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        bad += sum(g[i] * m['nrm'][m['idx'][t]][i] for i in range(3)) <= 0
    assert bad == 0, bad
    assert len(dxt5_solid(256, 256, lambda u, v: (255, 0, 0), 0)) == 65536
    print('chart ok:', len(m['pos']), 'vertices,', len(m['idx']) // 3, 'triangles')
