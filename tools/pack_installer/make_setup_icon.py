"""Writes the setup's icon: src/app/pulse.ico without the 96/128 px images.

The bootstrapper draws its logo from the 256 px image (LoadIconWithScaleDown),
so the intermediate sizes only add ~31 KB of incompressible PNG data.
usage: make_setup_icon.py <pulse.ico> <out.ico>
"""
import struct
import sys

KEEP = {16, 20, 24, 32, 40, 48, 64, 256}


def main(src: str, dst: str) -> None:
    data = open(src, "rb").read()
    reserved, kind, count = struct.unpack_from("<HHH", data, 0)
    entries = []
    for i in range(count):
        w, h, colors, res, planes, bpp, size, offset = struct.unpack_from("<BBBBHHII", data, 6 + i * 16)
        if (w or 256) in KEEP:
            entries.append((w, h, colors, res, planes, bpp, data[offset:offset + size]))
    out = bytearray(struct.pack("<HHH", reserved, kind, len(entries)))
    offset = 6 + 16 * len(entries)
    for w, h, colors, res, planes, bpp, blob in entries:
        out += struct.pack("<BBBBHHII", w, h, colors, res, planes, bpp, len(blob), offset)
        offset += len(blob)
    for entry in entries:
        out += entry[-1]
    with open(dst, "wb") as f:
        f.write(out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
