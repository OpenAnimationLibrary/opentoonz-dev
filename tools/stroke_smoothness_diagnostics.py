#!/usr/bin/env python3
"""Analyze grouped stroke CSVs or generate an analytic circle sample stream.

Geometry uses consecutive distinct positions; timing and pressure use every row.
Circle fits describe the samples, not the artist's intended/reference curve.
"""

import argparse
import csv
import math
from decimal import Decimal
from pathlib import Path
from statistics import median


GROUP_FIELDS = (
    "session", "stroke", "root_track", "track", "stage", "coordinate_space",
    "device_id", "touch_id",
)
NUMERIC_FIELDS = (
    "x", "y", "pressure", "thickness", "tilt_x", "tilt_y", "sample_time",
    "event_time", "pipeline_timestamp", "source_timestamp", "arrival_timestamp", "length",
)


def read_groups(path, stages=None):
    groups, parts = {}, {}
    with Path(path).open(newline="", encoding="utf-8-sig") as file:
        reader = csv.DictReader(file)
        if not {"x", "y"}.issubset(reader.fieldnames or ()):
            raise ValueError("CSV must contain x and y columns")
        fields = [key for key in GROUP_FIELDS if key in reader.fieldnames]
        for line, row in enumerate(reader, 2):
            if stages and row.get("stage") not in stages:
                continue
            for field in NUMERIC_FIELDS:
                value = row.get(field)
                if field not in ("x", "y") and value in (None, ""):
                    continue
                try:
                    valid = math.isfinite(float(value))
                except (TypeError, ValueError):
                    valid = False
                if not valid:
                    raise ValueError(f"line {line}: {field} must be finite")
            key = tuple((field, row.get(field, "")) for field in fields)
            part = parts.get(key, 0)
            groups.setdefault((key, part), []).append(row)
            # A final row also separates legacy streams with missing/reused IDs.
            if str(row.get("final", "")).strip().lower() in ("1", "true", "yes"):
                parts[key] = part + 1
    return groups


def distinct_positions(points):
    result = []
    for point in points:
        if not result or point != result[-1]:
            result.append(point)
    return result


def solve_3x3(a, b):
    m = [list(a[i]) + [b[i]] for i in range(3)]
    for col in range(3):
        pivot = max(range(col, 3), key=lambda r: abs(m[r][col]))
        if abs(m[pivot][col]) < 1e-12:
            raise ValueError("points are collinear or insufficiently distinct")
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
        raise ValueError("at least three distinct positions are required")
    # Normalize before fitting so a large scene offset does not dominate the fit.
    ox = sum(p[0] for p in points) / len(points)
    oy = sum(p[1] for p in points) / len(points)
    scale = max(math.hypot(x - ox, y - oy) for x, y in points)
    if not math.isfinite(scale) or scale <= 0:
        raise ValueError("positions do not define a finite circle")
    normalized = [((x - ox) / scale, (y - oy) / scale) for x, y in points]
    sx = sy = sxx = syy = sxy = sb0 = sb1 = sb2 = 0.0
    for x, y in normalized:
        z = -(x * x + y * y)
        sx += x
        sy += y
        sxx += x * x
        syy += y * y
        sxy += x * y
        sb0 += x * z
        sb1 += y * z
        sb2 += z
    d, e, f = solve_3x3(
        ((sxx, sxy, sx), (sxy, syy, sy), (sx, sy, len(points))),
        (sb0, sb1, sb2),
    )
    cx, cy = -d / 2, -e / 2
    r2 = cx * cx + cy * cy - f
    if not math.isfinite(r2) or r2 <= 0:
        raise ValueError("invalid fitted radius")
    return ox + scale * cx, oy + scale * cy, scale * math.sqrt(r2)


def turning_angles(points):
    points = distinct_positions(points)
    closed = len(points) > 2 and points[0] == points[-1]
    if closed:
        points = points[:-1]
    indices = range(len(points)) if closed else range(1, len(points) - 1)
    result = []
    for i in indices:
        prev, curr, following = points[i - 1], points[i], points[(i + 1) % len(points)]
        ax, ay = curr[0] - prev[0], curr[1] - prev[1]
        bx, by = following[0] - curr[0], following[1] - curr[1]
        if math.hypot(ax, ay) > 0 and math.hypot(bx, by) > 0:
            result.append(math.degrees(math.atan2(abs(ax * by - ay * bx), ax * bx + ay * by)))
    return result


def circle_metrics(points, circle):
    cx, cy, radius = circle
    errors = [abs(math.hypot(x - cx, y - cy) - radius) for x, y in points]
    segment_error = max(errors, default=0.0)
    for (ax, ay), (bx, by) in zip(points, points[1:]):
        dx, dy = bx - ax, by - ay
        length2 = dx * dx + dy * dy
        if length2 == 0:
            continue
        u = max(0.0, min(1.0, ((cx - ax) * dx + (cy - ay) * dy) / length2))
        nearest_radius = math.hypot(ax + u * dx - cx, ay + u * dy - cy)
        # Radius is largest at an endpoint and smallest at this projection.
        segment_error = max(segment_error, abs(nearest_radius - radius))
    return {
        "vertex_rms_radial_deviation": (
            math.hypot(*errors) / math.sqrt(len(errors)) if errors else None
        ),
        "vertex_max_radial_deviation": max(errors) if errors else None,
        "segment_max_radial_deviation": segment_error if len(points) > 1 else None,
    }


def analyze_group(rows, reference=None, stage=""):
    raw_points = [(float(row["x"]), float(row["y"])) for row in rows]
    points = distinct_positions(raw_points)
    result = {"rows": len(rows), "distinct_consecutive_positions": len(points)}
    result["same_position_pressure_changes"] = sum(
        a == b and left.get("pressure") not in (None, "")
        and right.get("pressure") not in (None, "")
        and float(left["pressure"]) != float(right["pressure"])
        for a, b, left, right in zip(raw_points, raw_points[1:], rows, rows[1:])
    )
    if any("forwarded" in row for row in rows):
        result["forwarded_rows"] = sum(
            str(row.get("forwarded", "")).strip().lower() in ("1", "true", "yes")
            for row in rows
        )
    for field in ("sample_time", "event_time", "pipeline_timestamp",
                  "source_timestamp", "arrival_timestamp"):
        convert = Decimal if field.endswith("timestamp") else float
        values = [convert(row[field]) for row in rows if row.get(field) not in (None, "")]
        if not values:
            continue
        factor = (1e-3 if field == "source_timestamp" else
                  1e-9 if field.endswith("timestamp") else 1.0)
        deltas = [float(b - a) * factor for a, b in zip(values, values[1:])]
        result[field + "_rows"] = len(values)
        result[field + "_median_delta_seconds"] = median(deltas) if deltas else None
        result[field + "_equal_pairs"] = sum(dt == 0 for dt in deltas)
        result[field + "_backward_pairs"] = sum(dt < 0 for dt in deltas)
    if stage == "fitted_control":
        result["geometry"] = "control points; drawn-curve metrics omitted (use fitted_curve)"
        return result
    angles = turning_angles(points)
    result["median_turning_angle_deg"] = median(angles) if angles else None
    result["max_turning_angle_deg"] = max(angles) if angles else None
    try:
        circle = reference if reference is not None else fit_circle(points)
        result["circle_basis"] = (
            "explicit reference" if reference is not None
            else "fitted vertices; not a reference curve"
        )
        result["circle_center_x"], result["circle_center_y"], result["circle_radius"] = circle
        result.update(circle_metrics(points, circle))
    except ValueError as error:
        result["circle"] = "unavailable: " + str(error)
    return result


def analyze(path, reference=None, stages=None):
    groups = read_groups(path, stages)
    if not groups:
        raise ValueError("no matching data rows")
    for (identity, part), rows in groups.items():
        label = ", ".join(f"{key}={value}" for key, value in identity) or "unidentified stream"
        print(f"\n[{label}; part={part + 1}]")
        for key, value in analyze_group(rows, reference, dict(identity).get("stage", "")).items():
            text = "n/a" if value is None else str(value)
            if isinstance(value, float):
                text = format(value, ".9g")
            print(f"{key}: {text}")


def generate_circle(path, rate, duration, radius, cx, cy):
    if not all(math.isfinite(value) for value in (rate, duration, radius, cx, cy)):
        raise ValueError("circle parameters must be finite")
    if min(rate, duration, radius) <= 0 or not math.isfinite(rate * duration):
        raise ValueError("rate, duration and radius must be positive and finite")
    intervals = round(rate * duration)
    if intervals < 3:
        raise ValueError("rate times duration must round to at least three intervals")
    fields = [
        "session", "stroke", "root_track", "track", "stage", "event",
        "coordinate_space", "pipeline_timestamp", "sample_time", "event_time",
        "x", "y", "pressure", "tilt_x", "tilt_y", "device_id", "touch_id",
        "has_pressure", "has_tilt", "final", "forwarded",
    ]
    with Path(path).open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        for i in range(intervals + 1):
            time = duration * i / intervals
            angle = 2 * math.pi * i / intervals if i < intervals else 0.0
            writer.writerow({
                "session": "synthetic", "stroke": 1, "root_track": 1, "track": 1,
                "stage": "synthetic", "event": i, "coordinate_space": "tool",
                "pipeline_timestamp": round(time * 1_000_000_000),
                "sample_time": format(time, ".17g"), "event_time": format(time, ".17g"),
                "x": format(cx + radius * math.cos(angle), ".17g"),
                "y": format(cy + radius * math.sin(angle), ".17g"),
                "pressure": 0.5, "tilt_x": 0, "tilt_y": 0, "device_id": 1,
                "touch_id": 0, "has_pressure": 1, "has_tilt": 0,
                "final": int(i == intervals), "forwarded": 1,
            })
    print(f"wrote {intervals + 1} samples covering 0 through {duration:g} seconds to {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    ap = sub.add_parser("analyze", help="analyze each stroke/track/stage separately")
    ap.add_argument("csv")
    ap.add_argument("--stage", action="append", help="include this stage (repeatable)")
    ap.add_argument(
        "--center", nargs=2, type=float, metavar=("CX", "CY"),
        help="explicit reference center in the logged coordinate space",
    )
    ap.add_argument("--radius", type=float, help="explicit reference radius; requires --center")
    cp = sub.add_parser(
        "circle", help="generate an analytic circle, including the closing endpoint"
    )
    cp.add_argument("csv")
    cp.add_argument("--rate", type=float, default=50.0, help="nominal sample intervals per second")
    cp.add_argument("--duration", type=float, default=0.4, help="seconds")
    cp.add_argument("--radius", type=float, default=150.0)
    cp.add_argument("--cx", type=float, default=0.0)
    cp.add_argument("--cy", type=float, default=0.0)
    args = parser.parse_args()
    try:
        if args.command == "circle":
            generate_circle(args.csv, args.rate, args.duration, args.radius, args.cx, args.cy)
        else:
            if (args.center is None) != (args.radius is None):
                raise ValueError("--center and --radius must be supplied together")
            reference = (*args.center, args.radius) if args.center is not None else None
            if reference is not None and (
                not all(map(math.isfinite, reference)) or args.radius <= 0
            ):
                raise ValueError("reference circle must be finite with a positive radius")
            analyze(args.csv, reference, args.stage)
    except (ValueError, OSError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    main()
