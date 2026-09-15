#!/usr/bin/env python3
"""Tile simulator framebuffer captures into one PNG for side-by-side review.

    tools/contact_sheet.py --cols 5 --scale 2 out.png a.ppm b.ppm ...

Captures are self-labelled by the simulator, so no text rendering is needed
here. Downscaling is a box filter over integer factors, which is enough for a
review sheet and keeps this dependency-free.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ppm2png import read_ppm, write_png  # noqa: E402


def box_downscale(w, h, rgb, factor):
    if factor <= 1:
        return w, h, rgb
    ow, oh = w // factor, h // factor
    out = bytearray(ow * oh * 3)
    n = factor * factor
    for y in range(oh):
        for x in range(ow):
            r = g = b = 0
            for dy in range(factor):
                row = (y * factor + dy) * w
                for dx in range(factor):
                    i = (row + x * factor + dx) * 3
                    r += rgb[i]
                    g += rgb[i + 1]
                    b += rgb[i + 2]
            o = (y * ow + x) * 3
            out[o] = r // n
            out[o + 1] = g // n
            out[o + 2] = b // n
    return ow, oh, bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cols", type=int, default=4)
    ap.add_argument("--scale", type=int, default=2, help="integer downscale factor")
    ap.add_argument("--gap", type=int, default=8)
    ap.add_argument("out")
    ap.add_argument("inputs", nargs="+")
    args = ap.parse_args()

    tiles = []
    for p in args.inputs:
        w, h, rgb = read_ppm(p)
        tiles.append(box_downscale(w, h, rgb, args.scale))

    tw, th = tiles[0][0], tiles[0][1]
    if any(t[0] != tw or t[1] != th for t in tiles):
        raise SystemExit("all captures must be the same size")

    cols = min(args.cols, len(tiles))
    rows = (len(tiles) + cols - 1) // cols
    gap = args.gap
    sheet_w = cols * tw + (cols + 1) * gap
    sheet_h = rows * th + (rows + 1) * gap

    sheet = bytearray(sheet_w * sheet_h * 3)  # black background

    for idx, (w, h, rgb) in enumerate(tiles):
        cx = idx % cols
        cy = idx // cols
        x0 = gap + cx * (tw + gap)
        y0 = gap + cy * (th + gap)
        for y in range(h):
            src = y * w * 3
            dst = ((y0 + y) * sheet_w + x0) * 3
            sheet[dst : dst + w * 3] = rgb[src : src + w * 3]

    write_png(args.out, sheet_w, sheet_h, bytes(sheet))
    print(f"{args.out}: {sheet_w}x{sheet_h} ({len(tiles)} tiles, {cols}x{rows})")


if __name__ == "__main__":
    main()
