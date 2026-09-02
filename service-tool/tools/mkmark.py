"""Rasterise img/industrygrow-mark.svg into an LVGL ARGB8888 C array.

The mark is a single path of straight segments (353 line-to commands, no
curves), so it can be filled exactly as polygons -- no SVG library needed, which
matters because cairo is not installed here.

fill-rule is evenodd, which for a set of subpaths is the XOR of their coverage,
so each subpath is filled into its own mask and XORed. Supersampled 8x and
box-downsampled to get the alpha channel.
"""
import re
import io
from PIL import Image, ImageDraw, ImageChops

SRC = r"E:\IndustryGrow\img\industrygrow-mark.svg"
OUT = r"E:\hmi-panel\igrow-panel\ui\ig_mark_img.c"
SIZE = 26          # pixels on the panel
SS = 8             # supersample factor
COLOUR = (0x4A, 0xDE, 0x80)   # brand green, the mark's own dark-mode fill

svg = io.open(SRC, encoding="utf-8").read()

vb = re.search(r'viewBox="([\d.\-eE ]+)"', svg).group(1).split()
vx, vy, vw, vh = (float(v) for v in vb)

d = re.search(r'\sd="([^"]+)"', svg).group(1)

# Split into subpaths on M/m, then read the coordinate pairs.
subpaths = []
for chunk in re.split(r"[Mm]", d):
    chunk = chunk.strip()
    if not chunk:
        continue
    nums = [float(n) for n in re.findall(r"-?\d*\.?\d+(?:[eE][-+]?\d+)?", chunk)]
    pts = list(zip(nums[0::2], nums[1::2]))
    if len(pts) >= 3:
        subpaths.append(pts)

assert subpaths, "no subpaths parsed"

big = SIZE * SS
scale = big / max(vw, vh)

acc = Image.new("1", (big, big), 0)
for pts in subpaths:
    layer = Image.new("1", (big, big), 0)
    dr = ImageDraw.Draw(layer)
    dr.polygon([((x - vx) * scale, (y - vy) * scale) for x, y in pts], fill=1)
    acc = ImageChops.logical_xor(acc, layer)   # evenodd == XOR of subpath coverage

alpha = acc.convert("L").resize((SIZE, SIZE), Image.LANCZOS)

rows = []
px = alpha.load()
for y in range(SIZE):
    vals = []
    for x in range(SIZE):
        a = px[x, y]
        # LVGL ARGB8888 is a little-endian ARGB word, i.e. B, G, R, A in memory.
        # Premultiplication is not used, so plain colour with the coverage alpha.
        vals += [COLOUR[2], COLOUR[1], COLOUR[0], a]
    rows.append(", ".join("0x%02X" % v for v in vals))

body = ",\n    ".join(rows)

io.open(OUT, "w", encoding="utf-8", newline="").write(f"""/* SPDX-License-Identifier: CC-BY-SA-4.0
 *
 * GENERATED -- do not edit. Regenerate with tools/mkmark.py.
 *
 * The IndustryGrow mark from img/industrygrow-mark.svg, rasterised to
 * {SIZE}x{SIZE} ARGB8888 in the mark's own dark-mode green (#4ADE80). The source
 * SVG is the versioned brand asset; this is a build artifact of it, not a
 * second copy of the brand.
 */
#include "lvgl.h"

static const uint8_t ig_mark_map[] = {{
    {body}
}};

const lv_image_dsc_t ig_mark_img = {{
    .header = {{
        .magic  = LV_IMAGE_HEADER_MAGIC,
        .cf     = LV_COLOR_FORMAT_ARGB8888,
        .flags  = 0,
        .w      = {SIZE},
        .h      = {SIZE},
        .stride = {SIZE} * 4,
    }},
    .data_size = sizeof(ig_mark_map),
    .data      = ig_mark_map,
}};
""")
print(f"wrote {OUT}: {SIZE}x{SIZE}, {len(subpaths)} subpaths, {SIZE*SIZE*4} bytes of pixels")
