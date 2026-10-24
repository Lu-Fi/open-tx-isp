#!/usr/bin/env python3
"""Synthetic Bayer test chart for RAW injection into the ISP (all SoCs).

Generates a RAW frame (16-bit little endian container, one pixel per word,
LSB- or MSB-aligned) in the Bayer phase and bit depth of the sensor, plus a
JSON map with the patch rectangles and the expected linear RGB values, so
that evalchart.py can score the ISP output (snapshot/encoder frame).

Chart: 24 ColorChecker patches (4x6, upper part), a 16-step grey ramp,
black and white fields (bottom row).  Patch values are linear sRGB
(BabelColor averages, D65) scaled by --scale of full scale; the camera
Bayer channels receive R/G/B directly (no spectral model), so a neutral
patch is neutral in the raw and the AWB has nothing to correct unless
--wb r,b is given (simulated illuminant: raw R and B are divided by the
given gains, a correct AWB must multiply them back).

usage: rawchart.py -W 2880 -H 1620 --bayer bggr --bits 10 [--align lsb|msb]
                   [--scale 0.8] [--wb 1.0,1.0] -o chart.raw16 [-m chart.json]
"""
import argparse, json, struct, sys

# ColorChecker Classic, sRGB 8-bit (BabelColor 30-chart average, D65)
CC_SRGB = [
    (115, 82, 68), (194, 150, 130), (98, 122, 157), (87, 108, 67), (133, 128, 177), (103, 189, 170),
    (214, 126, 44), (80, 91, 166), (193, 90, 99), (94, 60, 108), (157, 188, 64), (224, 163, 46),
    (56, 61, 150), (70, 148, 73), (175, 54, 60), (231, 199, 31), (187, 86, 149), (8, 133, 161),
    (243, 243, 242), (200, 200, 200), (160, 160, 160), (122, 122, 121), (85, 85, 85), (52, 52, 52),
]
CC_NAMES = ["dark skin", "light skin", "blue sky", "foliage", "blue flower", "bluish green",
            "orange", "purplish blue", "moderate red", "purple", "yellow green", "orange yellow",
            "blue", "green", "red", "yellow", "magenta", "cyan",
            "white", "neutral 8", "neutral 6.5", "neutral 5", "neutral 3.5", "black"]


def srgb_to_lin(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def layout(W, H):
    """Return list of (name, x0, y0, x1, y1, (r,g,b) linear 0..1)."""
    patches = []
    mx, my = W // 40, H // 40
    cols, rows = 6, 4
    pw = (W - 2 * mx) // cols
    ph = int(H * 0.62) // rows
    for i, rgb in enumerate(CC_SRGB):
        r, c = divmod(i, cols)
        x0 = mx + c * pw
        y0 = my + r * ph
        patches.append((CC_NAMES[i], x0, y0, x0 + pw, y0 + ph, tuple(srgb_to_lin(v) for v in rgb)))
    # grey ramp: 16 steps, linear 0..1
    y0 = my + rows * ph + my
    y1 = y0 + int(H * 0.14)
    gw = (W - 2 * mx) // 16
    for i in range(16):
        v = i / 15.0
        x0 = mx + i * gw
        patches.append(("grey%02d" % i, x0, y0, x0 + gw, y1, (v, v, v)))
    # black / white fields at the bottom
    y0 = y1 + my
    y1 = min(H - my, y0 + int(H * 0.12))
    half = (W - 2 * mx) // 2
    patches.append(("black", mx, y0, mx + half, y1, (0.0, 0.0, 0.0)))
    patches.append(("white", mx + half, y0, mx + 2 * half, y1, (1.0, 1.0, 1.0)))
    return patches


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-W", type=int, required=True)
    ap.add_argument("-H", type=int, required=True)
    ap.add_argument("--bayer", default="bggr", choices=["rggb", "bggr", "grbg", "gbrg"])
    ap.add_argument("--bits", type=int, default=10)
    ap.add_argument("--align", default="lsb", choices=["lsb", "msb"])
    ap.add_argument("--scale", type=float, default=0.8, help="full-scale fraction for linear 1.0")
    ap.add_argument("--wb", default="1.0,1.0", help="simulated illuminant gains r,b (raw divided by)")
    ap.add_argument("--black", type=int, default=0, help="black level to add (sensor pedestal)")
    ap.add_argument("--bg", type=float, default=0.18, help="background grey (linear)")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("-m", "--map", default=None)
    a = ap.parse_args()

    W, H = a.W, a.H
    full = (1 << a.bits) - 1
    wr, wb = (float(x) for x in a.wb.split(","))
    shift = (16 - a.bits) if a.align == "msb" else 0
    patches = layout(W, H)

    # per-pixel colour plane lookup
    phase = {"rggb": "RGGB", "bggr": "BGGR", "grbg": "GRBG", "gbrg": "GBRG"}[a.bayer]

    def code(lin, ch):
        v = lin * a.scale
        if ch == "R":
            v /= wr
        elif ch == "B":
            v /= wb
        v = int(round(v * full)) + a.black
        return max(0, min(full, v)) << shift

    bg = (a.bg, a.bg, a.bg)
    # row -> list of (x0, x1, rgb)
    rows = [[] for _ in range(H)]
    for name, x0, y0, x1, y1, rgb in patches:
        for y in range(y0, min(y1, H)):
            rows[y].append((x0, x1, rgb))

    out = open(a.out, "wb")
    for y in range(H):
        line = [0] * W
        for x in range(W):
            ch = phase[(y & 1) * 2 + (x & 1)]
            rgb = bg
            for x0, x1, prgb in rows[y]:
                if x0 <= x < x1:
                    rgb = prgb
                    break
            line[x] = code(rgb["RGB".index(ch)], ch)
        out.write(struct.pack("<%dH" % W, *line))
    out.close()

    m = {"width": W, "height": H, "bayer": a.bayer, "bits": a.bits, "align": a.align,
         "scale": a.scale, "wb": [wr, wb], "black": a.black,
         "patches": [{"name": n, "rect": [x0, y0, x1, y1], "lin": list(rgb)} for n, x0, y0, x1, y1, rgb in patches]}
    with open(a.map or (a.out + ".json"), "w") as f:
        json.dump(m, f, indent=1)
    print("wrote %s (%d bytes) and map with %d patches" % (a.out, W * H * 2, len(patches)))


if __name__ == "__main__":
    main()
