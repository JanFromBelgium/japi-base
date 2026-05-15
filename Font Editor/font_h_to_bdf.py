#!/usr/bin/env python3
"""
Convert font_8x12.h (Japi Base C header) to BDF bitmap font format.
Usage: python3 font_h_to_bdf.py font_8x12.h japi_8x12.bdf
"""

import re
import sys

def parse_font_h(filename):
    with open(filename, 'r') as f:
        text = f.read()

    # Find all 12-byte hex groups: {0xNN,0xNN,...,0xNN}
    pattern = r'\{((?:0x[0-9A-Fa-f]{2},?[\s]*){12})\}'
    matches = re.findall(pattern, text)

    glyphs = []
    for m in matches:
        hexvals = re.findall(r'0x([0-9A-Fa-f]{2})', m)
        glyphs.append([int(h, 16) for h in hexvals])

    return glyphs


def write_bdf(glyphs, filename, width=8, height=12):
    with open(filename, 'w') as f:
        f.write("STARTFONT 2.1\n")
        f.write("FONT -Japi-Base-Medium-R-Normal--12-120-72-72-C-80-IBM-CP437\n")
        f.write(f"SIZE {height} 72 72\n")
        f.write(f"FONTBOUNDINGBOX {width} {height} 0 0\n")
        f.write("STARTPROPERTIES 5\n")
        f.write(f"PIXEL_SIZE {height}\n")
        f.write(f"FONT_ASCENT {height}\n")
        f.write("FONT_DESCENT 0\n")
        f.write("DEFAULT_CHAR 0\n")
        f.write("SPACING \"C\"\n")
        f.write("ENDPROPERTIES\n")
        f.write(f"CHARS {len(glyphs)}\n")

        for i, glyph in enumerate(glyphs):
            # Use CP437 name or fallback
            name = f"char{i:02X}"
            if 0x20 <= i <= 0x7E:
                name = chr(i) if chr(i).isalnum() else f"char{i:02X}"

            f.write(f"STARTCHAR {name}\n")
            f.write(f"ENCODING {i}\n")
            f.write(f"SWIDTH 500 0\n")
            f.write(f"DWIDTH {width} 0\n")
            f.write(f"BBX {width} {height} 0 0\n")
            f.write("BITMAP\n")
            for row in glyph:
                f.write(f"{row:02X}\n")
            f.write("ENDCHAR\n")

        f.write("ENDFONT\n")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <font_8x12.h> <output.bdf>")
        sys.exit(1)

    glyphs = parse_font_h(sys.argv[1])
    print(f"Parsed {len(glyphs)} glyphs from {sys.argv[1]}")

    if len(glyphs) != 256:
        print(f"WARNING: expected 256 glyphs, got {len(glyphs)}")

    write_bdf(glyphs, sys.argv[2])
    print(f"Written BDF to {sys.argv[2]}")
