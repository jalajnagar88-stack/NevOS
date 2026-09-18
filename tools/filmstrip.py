#!/usr/bin/env python3
"""Join simulator captures into one horizontal strip, for CSS frame animation.

    tools/filmstrip.py --scale 3 out.png frames/f*.ppm

A strip plus `animation: steps(N)` is how the device's motion gets shown
without an encoder: this container has no ffmpeg, no ImageMagick and no
Pillow, and a hand-written GIF encoder is a colour-quantiser and an LZW
compressor to get wrong. Stacking frames side by side is a memcpy, and the
browser does the animating.

Frames must all be the same size, which they are: the framebuffer does not
change shape.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from contact_sheet import box_downscale  # noqa: E402
from ppm2png import read_ppm, write_png  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scale", type=int, default=3, help="integer downscale factor")
    ap.add_argument("--every", type=int, default=1, help="keep every Nth frame")
    ap.add_argument("out")
    ap.add_argument("frames", nargs="+")
    args = ap.parse_args()

    kept = args.frames[:: args.every]
    if not kept:
        raise SystemExit("no frames")

    tiles = []
    for path in kept:
        w, h, rgb = read_ppm(path)
        tiles.append(box_downscale(w, h, rgb, args.scale))

    tw, th = tiles[0][0], tiles[0][1]
    if any(t[0] != tw or t[1] != th for t in tiles):
        raise SystemExit("all captures must be the same size")

    # One row, so the CSS only has to move background-position horizontally.
    out = bytearray(tw * len(tiles) * th * 3)
    stride = tw * len(tiles) * 3
    for index, (_, _, rgb) in enumerate(tiles):
        x0 = index * tw * 3
        for y in range(th):
            src = y * tw * 3
            dst = y * stride + x0
            out[dst : dst + tw * 3] = rgb[src : src + tw * 3]

    write_png(args.out, tw * len(tiles), th, bytes(out))
    print(f"{args.out}: {len(tiles)} frames of {tw}x{th}")


if __name__ == "__main__":
    main()
