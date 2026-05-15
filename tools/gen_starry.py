#!/usr/bin/env python3
"""Convert Starry Night JPG to 832x624 dithered C array for JAPI 64-colour palette.

JAPI palette: bits 5-4 = R, 3-2 = G, 1-0 = B. Each channel 4 levels mapped to
analog voltages 0/0.20/0.43/0.63 V (per Context). Use those as perceptual output
levels for FS dither error distribution.
"""
from PIL import Image
import sys

SRC = "/home/jan/.claude/jobs/c986de0e/starry.jpg"
DST_HDR = sys.argv[1] if len(sys.argv) > 1 else "starry_image.h"
W, H = 416, 312   # logical pixels; renderer expands 2x to 832x624 on screen

# Per-channel output values in 8-bit linear space, scaled from the analog
# voltages 0/0.20/0.43/0.63 V (max=0.63 V -> 255).
LEVELS_8 = [0, int(round(0.20/0.63 * 255)),
               int(round(0.43/0.63 * 255)),
               255]
print("Channel levels (8-bit):", LEVELS_8)

def quantize(v):
    """Pick closest 4-level index 0..3 for an 8-bit channel value."""
    best, best_d = 0, 1e9
    for i, lv in enumerate(LEVELS_8):
        d = abs(v - lv)
        if d < best_d:
            best_d = d; best = i
    return best

# Load image.
im = Image.open(SRC).convert("RGB")
sw, sh = im.size

# Trim the pale painting/frame edge from the Google Art Project scan.
# Equal fraction on all four sides keeps the aspect ratio (so the 4:3 fit
# below still fills edge-to-edge). ~5.5% removes the ~18-logical-px border.
INSET = 0.055
ix, iy = int(sw * INSET), int(sh * INSET)
im = im.crop((ix, iy, sw - ix, sh - iy))
sw, sh = im.size

# Crop to 4:3 + resize.
target_h = int(round(sw * H / W))   # 4:3 of cropped width
if target_h < sh:
    off = (sh - target_h) // 2
    im = im.crop((0, off, sw, off + target_h))
else:
    target_w = int(round(sh * W / H))
    off = (sw - target_w) // 2
    im = im.crop((off, 0, off + target_w, sh))

im = im.resize((W, H), Image.LANCZOS)
px = [list(t) for t in im.getdata()]   # flat list of [r,g,b]

# Floyd-Steinberg dither in linear 8-bit space, per channel.
# Buffer holds residual error as floats.
def at(x, y, c):
    return px[y * W + x][c]
def setp(x, y, c, v):
    px[y * W + x][c] = v

out = bytearray(W * H)
for y in range(H):
    for x in range(W):
        for c in range(3):
            old = at(x, y, c)
            old = max(0, min(255, old))
            qi = quantize(old)
            new = LEVELS_8[qi]
            setp(x, y, c, new)
            err = old - new
            if x + 1 < W:
                px[y*W + x+1][c] = max(0, min(255, int(at(x+1, y, c) + err * 7/16)))
            if y + 1 < H:
                if x > 0:
                    px[(y+1)*W + x-1][c] = max(0, min(255, int(at(x-1, y+1, c) + err * 3/16)))
                px[(y+1)*W + x][c] = max(0, min(255, int(at(x, y+1, c) + err * 5/16)))
                if x + 1 < W:
                    px[(y+1)*W + x+1][c] = max(0, min(255, int(at(x+1, y+1, c) + err * 1/16)))
        # Encode JAPI palette byte
        r, g, b = px[y*W + x]
        ri = LEVELS_8.index(r)
        gi = LEVELS_8.index(g)
        bi = LEVELS_8.index(b)
        out[y*W + x] = (ri << 4) | (gi << 2) | bi
    if y % 40 == 0:
        print(f"  row {y}/{H}")

# Emit header
with open(DST_HDR, "w") as f:
    f.write(f"// Generated from starry.jpg - {W}x{H}, JAPI 64-colour palette, FS dither.\n")
    f.write(f"static const uint8_t starry_image[{W} * {H}] = {{\n")
    for i in range(0, W*H, 16):
        chunk = ", ".join(f"0x{b:02X}" for b in out[i:i+16])
        f.write("    " + chunk + ",\n")
    f.write("};\n")
print(f"Wrote {DST_HDR}: {W*H} bytes")
