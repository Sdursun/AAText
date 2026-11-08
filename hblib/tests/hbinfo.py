# PC side of hblib/tests/hbinfo.c: the same strings shaped with HarfBuzz
# (uharfbuzz), printed the same way. Lines starting with '#' differ.
#   python hbinfo.py font.ttf
import sys
import uharfbuzz as hb

TESTS = [
    "AVATAR Wave To. Yo, P.T.",
    "office fine flow affix",
    "Pijamal\u0131 hasta ya\u011f\u0131z \u015fof\u00f6re \u00e7abucak "
    "g\u00fcvendi. \u0130\u011e\u015e\u00c7\u00d6\u00dc",
    "e\u0301 a\u0308 o\u0303 n\u0327",
    "\u0645\u0631\u062d\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645",
]

data = open(sys.argv[1], 'rb').read()
face = hb.Face(data)
font = hb.Font(face)
font.scale = (face.upem, face.upem)
print(f"# HarfBuzz {getattr(hb, "__harfbuzz_version__", "(uharfbuzz)")}")
print(f"font: {face.glyph_count} glyphs, {face.upem} units per em")
for i, t in enumerate(TESTS):
    buf = hb.Buffer()
    buf.add_utf8(t.encode('utf-8'))   # clusters are byte offsets, as in C
    buf.language = 'ar' if i == len(TESTS) - 1 else 'en'
    buf.guess_segment_properties()
    hb.shape(font, buf)
    info, pos = buf.glyph_infos, buf.glyph_positions
    d = {4: 'ltr', 5: 'rtl', 6: 'ttb', 7: 'btt'}.get(int(buf.direction), str(buf.direction)) \
        if not isinstance(buf.direction, str) else buf.direction
    print(f"text {i + 1}: {len(info)} glyphs, {d} {buf.language or '-'}")
    for g, p in zip(info, pos):
        print(f"  g={g.codepoint} c={g.cluster} ax={p.x_advance} ay={p.y_advance}"
              f" dx={p.x_offset} dy={p.y_offset}")
