#!/usr/bin/env python3
"""
Convert BDF bitmap font back to font_8x12.h (Japi Base C header).
Usage: python3 bdf_to_font_h.py japi_8x12.bdf font_8x12.h

Only processes the first 256 characters (encoding 0-255).
Expects 8x12 glyphs. Glyphs with different sizes are padded/cropped.
"""

import re
import sys

# CP437 character names for comments
CP437_NAMES = {
    0x00: "Null", 0x01: "Smiley", 0x02: "Smiley I", 0x03: "Heart",
    0x04: "Diamond", 0x05: "Club", 0x06: "Spade", 0x07: "Dot",
    0x08: "Inv Dot", 0x09: "Circle", 0x0A: "Inv Cir", 0x0B: "Male",
    0x0C: "Female", 0x0D: "Note", 0x0E: "Notes", 0x0F: "Sun",
    0x10: "R Tri", 0x11: "L Tri", 0x12: "UpDown", 0x13: "!!",
    0x14: "Para", 0x15: "Section", 0x16: "Block", 0x17: "UpDown2",
    0x18: "Up", 0x19: "Down", 0x1A: "Right", 0x1B: "Left",
    0x1C: "Corner", 0x1D: "LR Arrow", 0x1E: "Up Tri", 0x1F: "Dn Tri",
    0x7F: "House",
    0xB0: "Light", 0xB1: "Medium", 0xB2: "Dark", 0xB3: "VLine",
    0xB4: "RJunc", 0xC0: "BL", 0xC4: "HLine", 0xBF: "TR",
    0xC8: "DBL BL", 0xDA: "TL", 0xD9: "BR", 0xDB: "Full Block",
    0xDC: "Low Half", 0xDD: "Left Half", 0xDE: "Right Half",
    0xDF: "Up Half", 0xFE: "Sm Block", 0xFF: "NBSP",
}


def parse_bdf(filename):
    glyphs = {}

    with open(filename, 'r') as f:
        lines = f.readlines()

    i = 0
    while i < len(lines):
        line = lines[i].strip()

        if line.startswith("STARTCHAR"):
            encoding = -1
            bitmap = []
            in_bitmap = False

            i += 1
            while i < len(lines):
                line = lines[i].strip()
                if line.startswith("ENCODING"):
                    encoding = int(line.split()[1])
                elif line == "BITMAP":
                    in_bitmap = True
                elif line == "ENDCHAR":
                    break
                elif in_bitmap:
                    bitmap.append(int(line, 16) & 0xFF)
                i += 1

            if 0 <= encoding <= 255:
                # Pad or crop to 12 rows
                while len(bitmap) < 12:
                    bitmap.append(0)
                bitmap = bitmap[:12]
                glyphs[encoding] = bitmap

        i += 1

    return glyphs


def write_font_h(glyphs, filename, width=8, height=12):
    with open(filename, 'w') as f:
        f.write("#ifndef FONT_8X12_H\n")
        f.write("#define FONT_8X12_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("/**\n")
        f.write(" * =========================================================================\n")
        f.write(" * JAPI BASE 8x12 BITMAP FONT\n")
        f.write(" * =========================================================================\n")
        f.write(" * This font is based on the classic CP437 (IBM PC) character set.\n")
        f.write(" * * STRUCTURE:\n")
        f.write(" * Each character is 8 pixels wide and 12 pixels high.\n")
        f.write(" * A character is represented by 12 bytes. Each byte represents one row.\n")
        f.write(" * The Most Significant Bit (MSB, 0x80) is the leftmost pixel.\n")
        f.write(" * * MEMORY PLACEMENT:\n")
        f.write(" * We use the \".data\" section attribute to ensure this font is stored \n")
        f.write(" * in RAM (SRAM). Accessing Flash (XIP) during VGA rendering can cause \n")
        f.write(" * \"jitter\" or timing issues due to flash latency.\n")
        f.write(" * =========================================================================\n")
        f.write(" */\n\n")
        f.write('uint8_t __attribute__((section(".data"))) font_8x12[256][12] = {\n')

        for i in range(256):
            glyph = glyphs.get(i, [0] * 12)
            hex_str = ",".join(f"0x{b:02X}" for b in glyph)

            # Comment with char code and name/character
            if i in CP437_NAMES:
                comment = f" // {i:02X} {CP437_NAMES[i]}"
            elif 0x20 <= i <= 0x7E:
                comment = f" // {i:02X} '{chr(i)}'"
            else:
                comment = f" // {i:02X}"

            comma = "," if i < 255 else ""
            f.write(f"    {{{hex_str}}}{comma}{comment}\n")

        f.write("};\n\n")
        f.write("#endif // FONT_8X12_H\n")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <input.bdf> <font_8x12.h>")
        sys.exit(1)

    glyphs = parse_bdf(sys.argv[1])
    print(f"Parsed {len(glyphs)} glyphs from {sys.argv[1]}")

    missing = [i for i in range(256) if i not in glyphs]
    if missing:
        print(f"WARNING: {len(missing)} missing glyphs (will be blank): "
              f"{', '.join(f'0x{x:02X}' for x in missing[:10])}{'...' if len(missing) > 10 else ''}")

    write_font_h(glyphs, sys.argv[2])
    print(f"Written C header to {sys.argv[2]}")
