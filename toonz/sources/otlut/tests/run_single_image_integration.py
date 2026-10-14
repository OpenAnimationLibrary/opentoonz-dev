#!/usr/bin/env python3
import argparse
import math
import os
import pathlib
import struct
import subprocess
import zlib


def png_chunk(kind, payload):
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_png(path, width, height, pixels):
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)  # filter type 0
        start = y * stride
        raw.extend(pixels[start:start + stride])

    data = bytearray(b"\x89PNG\r\n\x1a\n")
    data.extend(png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
    data.extend(png_chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
    data.extend(png_chunk(b"IEND", b""))
    pathlib.Path(path).write_bytes(data)


def make_pair(width=256, height=256):
    source = bytearray(width * height * 3)
    target = bytearray(width * height * 3)

    cols = rows = 16
    for y in range(height):
        for x in range(width):
            gx = min(cols - 1, x * cols // width)
            gy = min(rows - 1, y * rows // height)

            r = gx / (cols - 1)
            g = gy / (rows - 1)
            b = ((gx * 7 + gy * 11) % cols) / (cols - 1)

            # Smooth central region adds intermediate values.
            dx = x - width * 0.5
            dy = y - height * 0.5
            if dx * dx + dy * dy < (width * 0.22) ** 2:
                r = x / (width - 1)
                g = y / (height - 1)
                b = (x + y) / (width + height - 2)

            # Non-identity deterministic grade.
            tr = min(1.0, (r ** 0.90) * 1.08 + 0.015 * g)
            tg = min(1.0, (g ** 1.02) * 0.98 + 0.010 * b)
            tb = min(1.0, (b ** 1.08) * 0.92 + 0.035 * (1.0 - r))

            i = (y * width + x) * 3
            source[i:i+3] = bytes(round(v * 255) for v in (r, g, b))
            target[i:i+3] = bytes(round(v * 255) for v in (tr, tg, tb))

    return source, target


def parse_cube(path):
    size = None
    triples = []
    for raw_line in pathlib.Path(path).read_text(encoding="utf-8-sig").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        if fields[0] == "LUT_3D_SIZE":
            size = int(fields[1])
            continue
        if fields[0] in ("TITLE", "DOMAIN_MIN", "DOMAIN_MAX", "LUT_3D_INPUT_RANGE"):
            continue
        if len(fields) == 3:
            try:
                values = tuple(float(v) for v in fields)
            except ValueError:
                continue
            triples.append(values)

    if size is None:
        raise AssertionError("Generated .cube is missing LUT_3D_SIZE")
    if len(triples) != size ** 3:
        raise AssertionError(
            f"Generated .cube contains {len(triples)} entries; expected {size ** 3}"
        )

    for triple in triples:
        if not all(math.isfinite(v) and 0.0 <= v <= 1.0 for v in triple):
            raise AssertionError(f"Invalid LUT value: {triple}")

    return size, triples


def identity_value(index, size):
    r = index % size
    g = (index // size) % size
    b = index // (size * size)
    d = size - 1
    return (r / d, g / d, b / d)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--workdir", required=True)
    args = parser.parse_args()

    work = pathlib.Path(args.workdir)
    work.mkdir(parents=True, exist_ok=True)

    source_path = work / "colorful_source.png"
    target_path = work / "colorful_target.png"
    lut_path = work / "colorful_grade.cube"

    source, target = make_pair()
    write_png(source_path, 256, 256, source)
    write_png(target_path, 256, 256, target)

    completed = subprocess.run(
        [
            args.exe,
            "--source", str(source_path),
            "--target", str(target_path),
            "--output", str(lut_path),
            "--size", "33",
            "--title", "otlut colorful integration test",
        ],
        check=True,
        text=True,
        capture_output=True,
    )

    size, triples = parse_cube(lut_path)
    if size != 33:
        raise AssertionError(f"Expected 33^3 LUT, got {size}^3")

    changed = 0
    max_delta = 0.0
    for i, triple in enumerate(triples):
        identity = identity_value(i, size)
        delta = max(abs(triple[c] - identity[c]) for c in range(3))
        max_delta = max(max_delta, delta)
        if delta > 1e-5:
            changed += 1

    if changed == 0 or max_delta < 0.01:
        raise AssertionError("Generated LUT is effectively identity; image fitting did not take effect")

    if "Directly sampled LUT cells:" not in completed.stdout:
        raise AssertionError("CLI did not report LUT coverage")

    print(completed.stdout)
    print(f"Validated {len(triples)} LUT entries")
    print(f"Non-identity cells: {changed}")
    print(f"Maximum channel delta from identity: {max_delta:.6f}")
    print(f"Source fixture: {source_path}")
    print(f"Target fixture: {target_path}")
    print(f"Generated LUT: {lut_path}")


if __name__ == "__main__":
    main()
