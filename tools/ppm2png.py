#!/usr/bin/env python3
"""Convert a binary PPM (P6) framebuffer capture to PNG.

The simulator writes PPM because a PPM writer is twenty lines of C with no
dependencies. PNG is what a person or a CI artifact viewer actually wants, and
zlib is in the Python standard library, so no image library is needed here
either.

    tools/ppm2png.py shot.ppm shot.png
"""
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        raise SystemExit(f"{path}: not a binary PPM (P6)")

    # Header: three whitespace-separated integers after the magic, '#' comments allowed.
    fields, pos = [], 2
    while len(fields) < 3:
        while pos < len(data) and data[pos : pos + 1].isspace():
            pos += 1
        if data[pos : pos + 1] == b"#":
            while pos < len(data) and data[pos : pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos : pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1  # single whitespace byte before the pixel data

    width, height, maxval = fields
    if maxval != 255:
        raise SystemExit(f"{path}: only 8-bit PPM is supported (maxval={maxval})")
    expected = width * height * 3
    pixels = data[pos : pos + expected]
    if len(pixels) != expected:
        raise SystemExit(f"{path}: truncated, expected {expected} bytes, got {len(pixels)}")
    return width, height, pixels


def write_png(path, width, height, rgb):
    def chunk(tag, payload):
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    # Filter type 0 (None) in front of every scanline.
    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride : (y + 1) * stride] for y in range(height))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def main():
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} IN.ppm OUT.png")
    width, height, rgb = read_ppm(sys.argv[1])
    write_png(sys.argv[2], width, height, rgb)
    print(f"{sys.argv[2]}: {width}x{height}")


if __name__ == "__main__":
    main()
