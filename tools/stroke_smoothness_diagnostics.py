#!/usr/bin/env python3
"""Analyze or generate deterministic stroke-smoothness diagnostic CSV files.

This utility is intentionally independent of tablet drivers and brush behavior.
It provides the common data/metric side of the OpenToonz diagnostic protocol.
"""

import argparse
import csv
import math
from pathlib import Path
from statistics import median


def read_points(path):
    rows = list(csv.DictReader(Path(path).open(newline="", encoding="utf-8")))
    points = []
    for row in rows:
        try:
            points.append((float(row["x"]), float(row["y"])))
        except (KeyError, TypeError, ValueError):
            continue
    return rows, points


def solve_3x3(a, b):
    m = [list(a[i]) + [b[i]] for i in range(3)]
    for col in range(3):
        pivot = max(range(col, 3), key=lambda r: abs(m[r][col]))
        if abs(m[pivot][col]) < 1e-12:
            raise ValueError("circle fit is singular")
        m[col], m[pivot] = m[pivot], m[col]
        q = m[col][col]
        for j in range(col, 4):
            m[col][j] /= q
        for r in range(3):
            if r == col:
                continue
            q = m[r][col]
            for j in range(col, 4):
                m[r][j] -= q * m[col][j]
    return [m[i][3] for i in range(3)]


def fit_circle(points):
    if len(points) < 3:
        raise ValueError("at least three points are required")
    sx = sy = sxx = syy = sxy = 0.0
    sb0 = sb1 = sb2 = 0.0
    for x, y in points:
        z = -(x * x + y * y)
        sx += x
        sy += y
        sxx += x * x
        syy += y * y
        sxy += x * y
        sb0 += x * z
        sb1 += y * z
        sb2 += z
    n = float(len(points))
    d, e, f = solve_3x3(
        ((sxx, sxy, sx), (sxy, syy, sy), (sx, sy, n)),
        (sb0, sb1, sb2),
    )
    cx, cy = -d / 2.0, -e / 2.0
    r2 = cx * cx + cy * cy - f
    if r2 <= 0.0:
        raise ValueError("invalid fitted radius")
    return cx, cy, math.sqrt(r2)


def turning_angles(points):
    result = []
    for i in range(1, len(points) - 1):
        ax = points[i][0] - points[i - 1][0]
        ay = points[i][1] - points[i - 1][1]
        bx = points[i + 1][0] - points[i][0]
        by = points[i + 1][1] - points[i][1]
        la = math.hypot(ax, ay)
        lb = math.hypot(bx, by)
        if la <= 1e-12 or lb <= 1e-12:
            continue
        c = max(-1.0, min(1.0, (ax * bx + ay * by) / (la * lb)))
        result.append(math.degrees(math.acos(c)))
    return result


def analyze(path):
    rows, points = read_points(path)
    if len(points) < 3:
        raise SystemExit("not enough valid x/y rows")

    delivered = len(rows)
    if rows and "forwarded" in rows[0]:
        accepted = sum(
            str(row.get("forwarded", "")).strip().lower()
            in ("1", "true", "yes")
            for row in rows
        )
    else:
        accepted = delivered

    cx, cy, radius = fit_circle(points)
    deviations = [abs(math.hypot(x - cx, y - cy) - radius) for x, y in points]
    angles = turning_angles(points)
    rms = math.sqrt(sum(value * value for value in deviations) / len(deviations))

    print(f"rows: {delivered}")
    print(f"accepted: {accepted}")
    print(
        f"acceptance_ratio: {accepted / delivered:.6f}"
        if delivered
        else "acceptance_ratio: n/a"
    )
    print(f"best_fit_center: {cx:.6f},{cy:.6f}")
    print(f"best_fit_radius: {radius:.6f}")
    print(f"rms_radial_deviation: {rms:.6f}")
    print(f"max_radial_deviation: {max(deviations):.6f}")
    print(
        f"median_turning_angle_deg: {median(angles):.6f}"
        if angles
        else "median_turning_angle_deg: n/a"
    )
    print(
        f"max_turning_angle_deg: {max(angles):.6f}"
        if angles
        else "max_turning_angle_deg: n/a"
    )


def generate_circle(path, rate, duration, radius, cx, cy):
    count = max(3, int(round(rate * duration)))
    dt_ticks = 1_000_000_000.0 / rate
    with Path(path).open("w", newline="", encoding="utf-8") as file:
        fields = [
            "stroke",
            "event",
            "pipeline_timestamp",
            "x",
            "y",
            "pressure",
            "device_id",
            "final",
            "forwarded",
        ]
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        for i in range(count):
            angle = 2.0 * math.pi * i / count
            writer.writerow(
                {
                    "stroke": 1,
                    "event": i,
                    "pipeline_timestamp": f"{i * dt_ticks:.0f}",
                    "x": f"{cx + radius * math.cos(angle):.9f}",
                    "y": f"{cy + radius * math.sin(angle):.9f}",
                    "pressure": "0.5",
                    "device_id": 1,
                    "final": 1 if i == count - 1 else 0,
                    "forwarded": 1,
                }
            )
    print(f"wrote {count} replay samples to {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    analyze_parser = sub.add_parser(
        "analyze", help="analyze a raw or geometry CSV containing x/y"
    )
    analyze_parser.add_argument("csv")

    circle_parser = sub.add_parser(
        "circle", help="generate a TInputManager-compatible analytic circle stream"
    )
    circle_parser.add_argument("csv")
    circle_parser.add_argument(
        "--rate", type=float, default=50.0, help="samples per second"
    )
    circle_parser.add_argument("--duration", type=float, default=0.4, help="seconds")
    circle_parser.add_argument("--radius", type=float, default=150.0)
    circle_parser.add_argument("--cx", type=float, default=0.0)
    circle_parser.add_argument("--cy", type=float, default=0.0)

    args = parser.parse_args()
    if args.command == "analyze":
        analyze(args.csv)
    else:
        if args.rate <= 0 or args.duration <= 0 or args.radius <= 0:
            raise SystemExit("rate, duration and radius must be positive")
        generate_circle(args.csv, args.rate, args.duration, args.radius, args.cx, args.cy)


if __name__ == "__main__":
    main()
