#!/usr/bin/env python3
"""Coat colour preview for Real Genetics.

Mirrors src/genetics_palette.c exactly (same integer maths, same constants), applies the albino, red and black coat
transforms to real sprites from graphics/pokemon/<species>/ and renders a contact sheet.

    python3 tools/realgen/coat_preview.py [-o coat_preview.png] [--shiny] [species ...]
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

COAT_NORMAL, COAT_ALBINO, COAT_RED, COAT_BLACK = range(4)

# ---- constants: keep in sync with src/genetics_palette.c ----
OUTLINE_Y = 40          # luma below this = line art, kept dark
WHITE_Y = 200           # luma at or above this with low chroma = unpigmented white (spots, teeth, eye whites)
WHITE_CHROMA = 48
ALBINO_LO = 150         # pigmented ramp maps into ALBINO_LO..ALBINO_HI
ALBINO_HI = 248
ALBINO_HUE = 160        # cream (0..1535 hue scale, 256 = 60 degrees)
ALBINO_CHROMA = 26
ALBINO_RESIDUE_SHIFT = 3    # 1/8 of the original colour's chroma survives so regions stay separable
RED_HUE_BASE = 0        # pigment hues fold into a warm band: dark shades deep red, light shades apricot
RED_HUE_SPAN = 80       # spread from the original hue, keeps regions apart
RED_HUE_LUMA = 120      # extra hue added at full luma (toward orange)
RED_CHROMA_MIN = 120
BLACK_LO = 18           # pigmented ramp maps into BLACK_LO..BLACK_HI
BLACK_HI = 96
BLACK_CHROMA_NUM = 1    # surviving chroma fraction (num/den), keeps faint pattern like a melanistic leopard
BLACK_CHROMA_DEN = 6
BLACK_CHROMA_MAX = 16    # cap, so bright yellows do not turn olive
BLACK_OUTLINE_NUM = 1   # outline luma scale for black coats (stays darker than BLACK_LO)
BLACK_OUTLINE_DEN = 3


def expand5(v):
    return (v << 3) | (v >> 2)


def to5(v):
    if v < 0:
        v = 0
    if v > 255:
        v = 255
    return (v * 31 + 127) // 255


def luma(r, g, b):
    return (r * 77 + g * 150 + b * 29) >> 8


def hue(r, g, b):
    mx = max(r, g, b)
    mn = min(r, g, b)
    c = mx - mn
    if c == 0:
        return 0
    if mx == r:
        if g >= b:
            h = (256 * (g - b)) // c
        else:
            h = 1536 - (256 * (b - g)) // c
    elif mx == g:
        if b >= r:
            h = 512 + (256 * (b - r)) // c
        else:
            h = 512 - (256 * (r - b)) // c
    else:
        if r >= g:
            h = 1024 + (256 * (r - g)) // c
        else:
            h = 1024 - (256 * (g - r)) // c
    if h >= 1536:
        h -= 1536
    return h


def pure_hue(h):
    sector = h >> 8
    f = h & 255
    return [
        (255, f, 0),
        (255 - f, 255, 0),
        (0, 255, f),
        (0, 255 - f, 255),
        (f, 0, 255),
        (255, 0, 255 - f),
    ][sector]


def from_luma_hue_chroma(y, h, chroma):
    """A colour with luma exactly `y` (before clamping), hue `h` and chroma up to `chroma` (0..255)."""
    p = pure_hue(h)
    yp = luma(*p)
    # largest chroma that keeps every channel inside 0..255 at this luma
    for pi in p:
        d = pi - yp
        if d > 0:
            lim = ((255 - y) * 255) // d
        elif d < 0:
            lim = (y * 255) // (-d)
        else:
            continue
        if chroma > lim:
            chroma = lim
    out = []
    for pi in p:
        d = pi - yp
        if d >= 0:
            v = y + (d * chroma) // 255
        else:
            v = y - ((-d) * chroma) // 255
        out.append(max(0, min(255, v)))
    return out


def classify(r, g, b):
    y = luma(r, g, b)
    c = max(r, g, b) - min(r, g, b)
    if y < OUTLINE_Y:
        return "outline", y, c
    if y >= WHITE_Y and c < WHITE_CHROMA:
        return "white", y, c
    return "pigment", y, c


def apply_coat(pal5, coat):
    """pal5: list of 16 (r, g, b) 5-bit tuples. Returns a new list. Index 0 is never touched."""
    if coat == COAT_NORMAL:
        return list(pal5)
    cols = [tuple(expand5(v) for v in c) for c in pal5]
    lo, hi = 255, 0
    for i in range(1, 16):
        kind, y, _ = classify(*cols[i])
        if kind == "pigment":
            lo = min(lo, y)
            hi = max(hi, y)
    span = hi - lo if hi > lo else 0
    out = [pal5[0]]
    for i in range(1, 16):
        r, g, b = cols[i]
        kind, y, c = classify(r, g, b)
        if kind == "white":
            out.append(pal5[i])
            continue
        if span:
            t = ((y - lo) * 255) // span if kind == "pigment" else 0
        else:
            t = y
        if coat == COAT_ALBINO:
            if kind == "outline":
                out.append(pal5[i])
                continue
            ny = ALBINO_LO + (t * (ALBINO_HI - ALBINO_LO)) // 255
            nr, ng, nb = from_luma_hue_chroma(ny, ALBINO_HUE, ALBINO_CHROMA)
            # residue: original chroma vector, scaled down (signed shift done on magnitudes)
            res = []
            for v in (r, g, b):
                d = v - y
                res.append((d >> ALBINO_RESIDUE_SHIFT) if d >= 0 else -((-d) >> ALBINO_RESIDUE_SHIFT))
            nr, ng, nb = nr + res[0], ng + res[1], nb + res[2]
        elif coat == COAT_RED:
            if kind == "outline":
                out.append(pal5[i])
                continue
            nh = RED_HUE_BASE + (hue(r, g, b) * RED_HUE_SPAN) // 1536 + (y * RED_HUE_LUMA) // 255
            nc = c if c > RED_CHROMA_MIN else RED_CHROMA_MIN
            nr, ng, nb = from_luma_hue_chroma(y, nh, nc)
        else:  # COAT_BLACK
            if kind == "outline":
                ny = (y * BLACK_OUTLINE_NUM) // BLACK_OUTLINE_DEN
                nr = ng = nb = ny
            else:
                ny = BLACK_LO + (t * (BLACK_HI - BLACK_LO)) // 255
                nc = (c * BLACK_CHROMA_NUM) // BLACK_CHROMA_DEN
                if nc > BLACK_CHROMA_MAX:
                    nc = BLACK_CHROMA_MAX
                nr, ng, nb = from_luma_hue_chroma(ny, hue(r, g, b), nc)
        out.append((to5(nr), to5(ng), to5(nb)))
    return out


# ---- data loading and rendering ----

def load_pal(path):
    with open(path) as f:
        lines = f.read().split("\n")
    assert lines[0].strip() == "JASC-PAL", path
    n = int(lines[2])
    cols = []
    for ln in lines[3:3 + n]:
        r, g, b = (int(x) for x in ln.split())
        cols.append((r >> 3, g >> 3, b >> 3))
    while len(cols) < 16:
        cols.append((0, 0, 0))
    return cols[:16]


def render(species, pal5, scale):
    im = Image.open(os.path.join(ROOT, "graphics", "pokemon", species, "front.png"))
    assert im.mode == "P", species
    w, h = 64, 64   # first frame only (a few sprites are stacked strips)
    px = im.load()
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    po = out.load()
    for yy in range(h):
        for xx in range(w):
            idx = px[xx, yy] & 15
            if idx == 0:
                continue
            r, g, b = pal5[idx]
            po[xx, yy] = (expand5(r), expand5(g), expand5(b), 255)
    return out.resize((w * scale, h * scale), Image.NEAREST)


DEFAULT_SPECIES = [
    "pikachu", "charizard", "bulbasaur", "squirtle", "gengar", "umbreon",
    "mudkip", "torchic", "treecko", "gardevoir", "rayquaza", "absol",
    "eevee", "snorlax", "gyarados", "milotic", "salamence", "metagross",
    "ampharos", "houndoom", "ninetales", "lapras", "jigglypuff", "zigzagoon",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("species", nargs="*")
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "coat_preview.png"))
    ap.add_argument("--shiny", action="store_true", help="start from shiny.pal instead of normal.pal")
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--cols", type=int, default=4, help="species blocks per row")
    args = ap.parse_args()

    species = args.species or DEFAULT_SPECIES
    scale = args.scale
    cell = 64 * scale
    label_h = 18
    block_w = cell * 4 + 12
    rows = (len(species) + args.cols - 1) // args.cols
    head_h = 24
    sheet = Image.new("RGB", (block_w * args.cols + 12, head_h + rows * (cell + label_h + 8) + 8), (128, 136, 146))
    draw = ImageDraw.Draw(sheet)
    names = ["normal", "albino", "red", "black"]
    for bi in range(args.cols):
        for k, n in enumerate(names):
            draw.text((12 + bi * block_w + k * cell + 4, 6), n, fill=(255, 255, 255))
    for i, sp in enumerate(species):
        pal = load_pal(os.path.join(ROOT, "graphics", "pokemon", sp, "shiny.pal" if args.shiny else "normal.pal"))
        bx = 12 + (i % args.cols) * block_w
        by = head_h + (i // args.cols) * (cell + label_h + 8)
        draw.rectangle([bx - 2, by - 2, bx + cell * 4 + 1, by + cell + label_h], fill=(150, 158, 168))
        for k, coat in enumerate((COAT_NORMAL, COAT_ALBINO, COAT_RED, COAT_BLACK)):
            spr = render(sp, apply_coat(pal, coat), scale)
            sheet.paste(spr, (bx + k * cell, by), spr)
        draw.text((bx + 4, by + cell + 2), sp, fill=(20, 20, 20))
    sheet.save(args.out)
    print(args.out)


if __name__ == "__main__":
    sys.exit(main())
