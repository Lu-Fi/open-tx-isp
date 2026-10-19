#!/usr/bin/env python3
"""Score an ISP output picture (snapshot JPEG/PNG) against the chart map.

For every patch the mean sRGB of the central 50 % of the rectangle (scaled
from chart to picture size) is compared with the expected value
(linear -> sRGB encode, times --scale of the chart) as CIE76 dE in Lab.
Also prints the grey-ramp transfer (input linear -> output 8-bit) which
shows the effective gamma, and writes an optional side-by-side collage
"ideal | picture" (needs Pillow).

usage: evalchart.py chart.json picture.jpg [--collage out.png] [--expect-scale 1.0]
"""
import argparse, json, math, sys
try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("needs Pillow (pip install pillow)")


def lin_to_srgb(v):
    v = max(0.0, min(1.0, v))
    return 12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055


def srgb_to_lab(rgb):
    r, g, b = [(c / 255.0) for c in rgb]
    def inv(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = inv(r), inv(g), inv(b)
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = (0.2126 * r + 0.7152 * g + 0.0722 * b)
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    def f(t):
        return t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    fx, fy, fz = f(x), f(y), f(z)
    return 116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)


def de76(a, b):
    return math.sqrt(sum((p - q) ** 2 for p, q in zip(a, b)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("picture")
    ap.add_argument("--collage", default=None)
    ap.add_argument("--expect-scale", type=float, default=None,
                    help="linear multiplier for the expectation (default: chart scale)")
    a = ap.parse_args()
    m = json.load(open(a.map))
    im = Image.open(a.picture).convert("RGB")
    sx, sy = im.width / m["width"], im.height / m["height"]
    es = a.expect_scale if a.expect_scale is not None else m["scale"]
    px = im.load()
    rows, des, ramp = [], [], []
    for p in m["patches"]:
        x0, y0, x1, y1 = p["rect"]
        cx0, cy0 = x0 + (x1 - x0) * 0.25, y0 + (y1 - y0) * 0.25
        cx1, cy1 = x0 + (x1 - x0) * 0.75, y0 + (y1 - y0) * 0.75
        n, acc = 0, [0, 0, 0]
        for y in range(int(cy0 * sy), int(cy1 * sy)):
            for x in range(int(cx0 * sx), int(cx1 * sx)):
                c = px[x, y]
                acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; n += 1
        got = tuple(v / max(n, 1) for v in acc)
        exp = tuple(255 * lin_to_srgb(v * es) for v in p["lin"])
        d = de76(srgb_to_lab(got), srgb_to_lab(exp))
        rows.append((p["name"], exp, got, d))
        if p["name"].startswith("grey"):
            ramp.append((p["lin"][0], got))
        elif not p["name"] in ("black", "white"):
            des.append(d)
    print("%-14s %-16s %-16s %6s" % ("patch", "expected sRGB", "measured sRGB", "dE76"))
    for name, exp, got, d in rows:
        print("%-14s %3.0f,%3.0f,%3.0f      %3.0f,%3.0f,%3.0f      %6.1f" % ((name,) + exp + got + (d,)))
    print("ColorChecker mean dE76 = %.2f  max = %.2f" % (sum(des) / len(des), max(des)))
    print("grey ramp (linear in -> 8-bit out R,G,B):")
    print("  " + "  ".join("%.2f->%.0f/%.0f/%.0f" % (v, g[0], g[1], g[2]) for v, g in ramp))
    if a.collage:
        ideal = Image.new("RGB", (m["width"], m["height"]), tuple(int(255 * lin_to_srgb(0.18 * es)) for _ in range(3)))
        dr = ImageDraw.Draw(ideal)
        for p in m["patches"]:
            x0, y0, x1, y1 = p["rect"]
            dr.rectangle([x0, y0, x1 - 1, y1 - 1], fill=tuple(int(255 * lin_to_srgb(v * es)) for v in p["lin"]))
        h = 540
        ideal = ideal.resize((int(ideal.width * h / ideal.height), h))
        pic = im.resize((int(im.width * h / im.height), h))
        out = Image.new("RGB", (ideal.width + pic.width + 12, h + 24), (32, 32, 32))
        out.paste(ideal, (4, 20)); out.paste(pic, (ideal.width + 8, 20))
        d2 = ImageDraw.Draw(out)
        d2.text((6, 4), "ideal (expected)", fill=(255, 255, 255))
        d2.text((ideal.width + 10, 4), "ISP output: " + a.picture, fill=(255, 255, 255))
        out.save(a.collage)
        print("collage written:", a.collage)


if __name__ == "__main__":
    main()
