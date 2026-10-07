#!/usr/bin/env python3
"""
Builds an AmigaOS icon (.info) with a GlowIcon (OS 3.5+ colour icon)
from PNG images, and dumps existing icons.

  mkicon.py dump  <file.info> [out.png]
  mkicon.py tool  <normal.png> [selected.png] -o <out.info>
                  [--tooltype TEXT ...] [--stack N]

PNG input: 8 bit RGB or RGBA, not interlaced. Pixels with alpha < 128
are transparent. At most 255 colours (one palette entry is reserved for
transparency). The old-style image (for systems without GlowIcon
support) is made from the same picture in the four Workbench colours.

GlowIcon layout (the FORM ICON appended to the classic .info):
  FACE  width-1, height-1, flags, aspect, maxpalettebytes-1
  IMAG  transparent, numcolours-1, flags (1 transparent, 2 palette),
        image format (1 = RLE), palette format, depth, imagesize-1,
        palettesize-1, then image and palette data
RLE is PackBits over a stream of 'depth' bit values.
"""
import struct
import sys
import zlib

# ---------------------------------------------------------------- PNG


def png_read(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit(f'{path}: not a PNG file')
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, typ = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b'IHDR':
            w, h, bits, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
            if bits != 8 or ctype not in (2, 6) or inter:
                sys.exit(f'{path}: need 8 bit RGB/RGBA, not interlaced')
        elif typ == b'IDAT':
            idat += body
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[x] = (line[x] + pr) & 255
        rows.append(line)
        prev = line
    pix = []
    for line in rows:
        for x in range(w):
            p = line[x * bpp:(x + 1) * bpp]
            pix.append((p[0], p[1], p[2], p[3] if bpp == 4 else 255))
    return w, h, pix


def png_write(path, w, h, pix):
    raw = b''.join(b'\0' + bytes(c for p in pix[y * w:(y + 1) * w] for c in p)
                   for y in range(h))

    def chunk(t, d):
        return (struct.pack('>I', len(d)) + t + d +
                struct.pack('>I', zlib.crc32(t + d) & 0xffffffff))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' +
                           chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) +
                           chunk(b'IEND', b''))

# ---------------------------------------------------------------- RLE


class BitWriter:
    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0

    def put(self, v, bits):
        for i in range(bits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((v >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc, self.n = 0, 0

    def data(self):
        if self.n:
            return bytes(self.out) + bytes([self.acc << (8 - self.n)])
        return bytes(self.out)


class BitReader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def get(self, bits):
        v = 0
        for _ in range(bits):
            byte = self.data[self.pos >> 3] if (self.pos >> 3) < len(self.data) else 0
            v = (v << 1) | ((byte >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v


def rle_encode(values, depth):
    bw, i, n = BitWriter(), 0, len(values)
    while i < n:
        run = 1
        while i + run < n and run < 128 and values[i + run] == values[i]:
            run += 1
        if run >= 3:
            bw.put(257 - run, 8)            # -(run-1)
            bw.put(values[i], depth)
            i += run
            continue
        j = i
        while j < n and j - i < 128:
            if j + 2 < n and values[j] == values[j + 1] == values[j + 2]:
                break
            j += 1
        bw.put(j - i - 1, 8)
        for v in values[i:j]:
            bw.put(v, depth)
        i = j
    return bw.data()


def rle_decode(data, depth, count):
    br, out = BitReader(data), []
    while len(out) < count:
        c = br.get(8)
        if c < 128:
            out.extend(br.get(depth) for _ in range(c + 1))
        elif c > 128:
            v = br.get(depth)
            out.extend([v] * (257 - c))
    return out[:count]

# ---------------------------------------------------------------- dump


def dump(path, pngout=None):
    d = open(path, 'rb').read()
    magic, ver = struct.unpack('>HH', d[:4])
    gad = d[4:48]
    w, h = struct.unpack('>hh', gad[8:12])[0:2]
    flags, act, gtype, render, select = struct.unpack('>HHHII', gad[12:26])
    userdata = struct.unpack('>I', gad[40:44])[0]
    dtype = d[48]
    deftool, tooltypes, cx, cy, drawer, toolwin, stack = \
        struct.unpack('>IIiiIII', d[50:78])
    print(f'magic {magic:#x} version {ver} type {dtype} gadget {w}x{h} '
          f'flags {flags:#x} userdata {userdata} stack {stack}')
    pos = 78
    if drawer:
        pos += 56
    for name, ptr in (('normal', render), ('selected', select)):
        if not ptr:
            continue
        l, t, iw, ih, depth = struct.unpack('>hhhhh', d[pos:pos + 10])
        pos += 20
        size = depth * ih * ((iw + 15) // 16 * 2)
        print(f'planar {name}: {iw}x{ih}x{depth}')
        pos += size
    if deftool:
        n = struct.unpack('>I', d[pos:pos + 4])[0]
        print('default tool', d[pos + 4:pos + 4 + n - 1])
        pos += 4 + n
    if tooltypes:
        n = struct.unpack('>I', d[pos:pos + 4])[0] // 4 - 1
        pos += 4
        for _ in range(n):
            k = struct.unpack('>I', d[pos:pos + 4])[0]
            print('tooltype', d[pos + 4:pos + 4 + k - 1])
            pos += 4 + k
    if toolwin:
        n = struct.unpack('>I', d[pos:pos + 4])[0]
        pos += 4 + n
    if drawer and (userdata & 255) == 1:
        pos += 6
    rest = d[pos:]
    if rest[:4] != b'FORM' or rest[8:12] != b'ICON':
        print(f'no GlowIcon ({len(rest)} bytes left)')
        return
    end = 8 + struct.unpack('>I', rest[4:8])[0]
    p, face, images = 12, None, []
    while p < end:
        cid, n = struct.unpack('>4sI', rest[p:p + 8])
        body = rest[p + 8:p + 8 + n]
        if cid == b'FACE':
            face = (body[0] + 1, body[1] + 1, body[2], body[3],
                    struct.unpack('>H', body[4:6])[0] + 1)
            print(f'FACE {face[0]}x{face[1]} flags {face[2]} aspect '
                  f'{face[3]:#x} maxpalbytes {face[4]}')
        elif cid == b'IMAG':
            tr, nc, fl, ifmt, pfmt, depth, isz, psz = \
                struct.unpack('>BBBBBBHH', body[:10])
            nc += 1
            idata = body[10:10 + isz + 1]
            print(f'IMAG transparent {tr} colours {nc} flags {fl} imgfmt '
                  f'{ifmt} palfmt {pfmt} depth {depth} '
                  f'sizes {isz + 1}/{psz + 1 if fl & 2 else 0}')
            count = face[0] * face[1]
            idx = rle_decode(idata, depth, count) if ifmt else list(idata[:count])
            pal = None
            if fl & 2:
                pdata = body[10 + isz + 1:10 + isz + 1 + psz + 1]
                pb = rle_decode(pdata, 8, nc * 3) if pfmt else list(pdata[:nc * 3])
                pal = [tuple(pb[i * 3:i * 3 + 3]) for i in range(nc)]
            images.append((idx, pal, tr if fl & 1 else -1))
        p += 8 + n + (n & 1)
    if pngout and images:
        idx, pal, tr = images[0]
        pix = [(0, 0, 0, 0) if i == tr else pal[i] + (255,) for i in idx]
        png_write(pngout, face[0], face[1], pix)
        print('wrote', pngout)

# ---------------------------------------------------------------- write

# Workbench 3.x default colours for the old-style image
WB_COLOURS = [(0xAA, 0xAA, 0xAA), (0x00, 0x00, 0x00),
              (0xFF, 0xFF, 0xFF), (0x66, 0x88, 0xBB)]


def nearest(c, colours):
    return min(range(len(colours)),
               key=lambda i: sum((a - b) ** 2 for a, b in zip(c, colours[i])))


def planar(w, h, pix):
    """2 bitplanes in the Workbench colours; transparent = colour 0."""
    words = (w + 15) // 16
    planes = [bytearray(words * 2 * h) for _ in range(2)]
    for y in range(h):
        for x in range(w):
            r, g, b, a = pix[y * w + x]
            c = nearest((r, g, b), WB_COLOURS) if a >= 128 else 0
            for p in range(2):
                if c >> p & 1:
                    planes[p][y * words * 2 + x // 8] |= 0x80 >> (x & 7)
    return struct.pack('>hhhhhIBBI', 0, 0, w, h, 2, 1, 3, 0, 0) + \
        bytes(planes[0]) + bytes(planes[1])


def imag(w, h, pix, palette_index, palette):
    tr = 0
    idx = [tr if a < 128 else palette_index[(r, g, b)]
           for r, g, b, a in pix]
    depth = max(1, (len(palette) - 1).bit_length())
    img = rle_encode(idx, depth)
    pal = bytes(c for rgb in palette for c in rgb)
    palrle = rle_encode(list(pal), 8)
    pfmt = 1 if len(palrle) < len(pal) else 0
    if pfmt:
        pal = palrle
    hdr = struct.pack('>BBBBBBHH', tr, len(palette) - 1, 3, 1, pfmt, depth,
                      len(img) - 1, len(pal) - 1)
    return hdr + img + pal


def iff_chunk(cid, body):
    return cid + struct.pack('>I', len(body)) + body + (b'\0' if len(body) & 1 else b'')


def build_tool(pngs, out, tooltypes, stack):
    images = [png_read(p) for p in pngs]
    w, h = images[0][0], images[0][1]
    for iw, ih, _ in images:
        if (iw, ih) != (w, h):
            sys.exit('all images must have the same size')
    if w > 256 or h > 256:
        sys.exit('at most 256x256 pixels')

    # one palette for both images; entry 0 is the transparent colour
    palette, palette_index = [(0, 0, 0)], {}
    for _, _, pix in images:
        for r, g, b, a in pix:
            if a >= 128 and (r, g, b) not in palette_index:
                palette_index[(r, g, b)] = len(palette)
                palette.append((r, g, b))
    if len(palette) > 256:
        sys.exit(f'{len(palette) - 1} colours, at most 255 allowed')

    sel = len(images) > 1
    gadget = struct.pack('>IhhhhHHHIIIIIHI', 0, 0, 0, w, h,
                         0x0006 if sel else 0x0004,   # GADGIMAGE (+ GADGHIMAGE)
                         0x0003, 0x0001,              # RELVERIFY|GADGIMMEDIATE, BOOLGADGET
                         1, 1 if sel else 0, 0, 0, 0, 0, 1)
    disk = struct.pack('>HH', 0xE310, 1) + gadget + \
        struct.pack('>BBIIiiIII', 3, 0, 0, 1 if tooltypes else 0,
                    -2147483648, -2147483648, 0, 0, stack)  # WBTOOL, NO_ICON_POSITION
    body = disk + planar(w, h, images[0][2])
    if sel:
        body += planar(w, h, images[1][2])
    if tooltypes:
        body += struct.pack('>I', (len(tooltypes) + 1) * 4)
        for t in tooltypes:
            s = t.encode('iso-8859-1') + b'\0'
            body += struct.pack('>I', len(s)) + s

    face = struct.pack('>BBBBH', w - 1, h - 1, 0, 0x11, len(palette) * 3 - 1)
    form = b'ICON' + iff_chunk(b'FACE', face)
    for _, _, pix in images:
        form += iff_chunk(b'IMAG', imag(w, h, pix, palette_index, palette))
    body += b'FORM' + struct.pack('>I', len(form)) + form
    open(out, 'wb').write(body)
    print(f'{out}: {w}x{h}, {len(palette) - 1} colours, {len(body)} bytes')


def main(argv):
    if len(argv) >= 2 and argv[0] == 'dump':
        dump(argv[1], argv[2] if len(argv) > 2 else None)
        return
    if argv and argv[0] == 'tool':
        pngs, out, tts, stack, i = [], None, [], 4096, 1
        while i < len(argv):
            a = argv[i]
            if a == '-o':
                out = argv[i + 1]
                i += 1
            elif a == '--tooltype':
                tts.append(argv[i + 1])
                i += 1
            elif a == '--stack':
                stack = int(argv[i + 1])
                i += 1
            else:
                pngs.append(a)
            i += 1
        if not out or not 1 <= len(pngs) <= 2:
            sys.exit(__doc__)
        build_tool(pngs, out, tts, stack)
        return
    sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
