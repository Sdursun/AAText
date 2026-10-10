#!/usr/bin/env python3
# Draws icons/AATextManager.png (+ _sel) from the AATextPrefs tile:
# the tile is kept, its face repainted with a white "A" and a gold
# arrow going down into a tray ("install"). Supersampled, then reduced
# to one palette for both images (31 colours + transparent = 32).
# Run from the project directory: python3 tools/mkmanagericon.py
import os, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicon import png_read

def png_write(path, w, h, pix):
    raw = b''.join(b'\0' + bytes(c for p in pix[y*w:(y+1)*w] for c in p)
                   for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' +
        chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
        chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

GREEN = (79, 175, 127)
WHITE = (255, 255, 255)
GOLD = (247, 191, 47)
GOLDD = (175, 111, 15)
TRAY = (63, 63, 79)
TRAYL = (143, 143, 159)

def inside_poly(x, y, pts):
    c = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]; xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            c = not c
        j = i
    return c

# shapes in units of the tile's face (0..1), drawn in order
def shapes(x0, y0, x1, y1):
    S = []
    S.append((WHITE, [(.02,.96),(.22,.06),(.38,.06),(.58,.96),(.43,.96),(.30,.38),(.17,.96)]))
    S.append((WHITE, [(.13,.62),(.47,.62),(.50,.77),(.10,.77)]))
    S.append((TRAY,  [(.58,.80),(.99,.80),(.99,.99),(.58,.99)]))
    S.append((TRAYL, [(.63,.80),(.94,.80),(.94,.91),(.63,.91)]))
    S.append((GOLDD, [(.66,.02),(.89,.02),(.89,.44),(.99,.44),(.775,.78),(.56,.44),(.66,.44)]))
    S.append((GOLD,  [(.70,.06),(.85,.06),(.85,.48),(.92,.48),(.775,.71),(.63,.48),(.70,.48)]))
    sx, sy = x1 - x0, y1 - y0
    return [(c, [(x0 + x * sx, y0 + y * sy) for x, y in p]) for c, p in S]

def render(base, w, h, box, face, GREEN):
    out = list(base)
    N = 4
    sh = shapes(*box)
    for y in range(h):
        for x in range(w):
            if not face(x, y):
                continue
            acc = [0, 0, 0]
            for sy in range(N):
                for sx in range(N):
                    px, py = x + (sx + .5) / N, y + (sy + .5) / N
                    col = GREEN
                    for c, p in sh:
                        if inside_poly(px, py, p):
                            col = c
                    for i in range(3):
                        acc[i] += col[i]
            out[y*w+x] = tuple(a // (N*N) for a in acc) + (255,)
    return out

def quantize(pix, keep, limit):
    # palette: the tile's own colours plus the drawn ones, then the most
    # frequent blends until the limit
    from collections import Counter
    pal = list(dict.fromkeys(keep))
    cnt = Counter(p[:3] for p in pix if p[3] >= 128 and p[:3] not in pal)
    for c, _ in cnt.most_common():
        if len(pal) >= limit:
            break
        if min(sum((a-b)**2 for a, b in zip(c, q)) for q in pal) > 300:
            pal.append(c)
    def near(c):
        return min(pal, key=lambda q: sum((a-b)**2 for a, b in zip(c, q)))
    return [p if p[3] < 128 else near(p[:3]) + (255,) for p in pix], len(pal)

# both images share one GlowIcon palette (mkicon.py): render both, then
# reduce them together to 31 colours (+ transparent = 32)
from collections import Counter
outs, keep = [], []
for src, dst in (('icons/AATextPrefs.png', 'icons/AATextManager.png'),
                 ('icons/AATextPrefs_sel.png', 'icons/AATextManager_sel.png')):
    w, h, pix = png_read(src)
    GREEN = Counter(p[:3] for p in pix if p[3] >= 128).most_common(1)[0][0]
    # the face: bounding box of the base green; everything inside is redrawn
    xs = [i % w for i, p in enumerate(pix) if p[3] >= 128 and p[:3] == GREEN]
    ys = [i // w for i, p in enumerate(pix) if p[3] >= 128 and p[:3] == GREEN]
    bx0, by0, bx1, by1 = min(xs), min(ys), max(xs) + 1, max(ys) + 1
    face = lambda x, y: bx0 <= x < bx1 and by0 <= y < by1
    m = 3   # margin inside the face
    out = render(pix, w, h, (bx0 + m, by0 + m, bx1 - m, by1 - m), face, GREEN)
    # the frame and shadow keep their exact colours
    keep += [pix[i][:3] for i in range(w * h)
             if pix[i][3] >= 128 and not face(i % w, i // w)]
    keep.append(GREEN)
    outs.append((dst, w, h, out))

keep = list(dict.fromkeys(keep + [WHITE, GOLD, GOLDD, TRAY, TRAYL]))
allpix = [p for _, _, _, o in outs for p in o]
allq, n = quantize(allpix, keep, 31)
for k, (dst, w, h, out) in enumerate(outs):
    q = allq[k * w * h:(k + 1) * w * h]
    png_write(dst, w, h, q)
    print(dst, 'colours (both images)', n)
    big = [q[(y // 8) * w + x // 8] for y in range(h * 8) for x in range(w * 8)]
    png_write(dst.replace('icons/', 'build/').replace('.png', '_x8.png'), w * 8, h * 8,
              [p if p[3] >= 128 else (200, 200, 200, 255) for p in big])
