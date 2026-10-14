#pragma once

#include "tgeometry.h"

#include <array>
#include <cmath>
#include <vector>

namespace TrailStamp {

// A click has no tangent or arc length. Give it two straight quadratic chunks
// pointing along +X, so the style's Rotation alone determines stamp
// orientation. One stage unit survives PLI coordinate quantization and is below
// the minimum stamp spacing (2 units) in both Trail renderers, even with
// negative Distance. Keep the first point at the click: that is where the
// renderer places stamp 0.
inline bool makeClickCarrier(const std::vector<TThickPoint> &points,
                             std::array<TThickPoint, 5> &carrier) {
  if (points.empty()) return false;
  const TThickPoint &first = points.front();
  double thickness         = 0.0;
  for (const TThickPoint &point : points) {
    // Test every position, not just the endpoints: a loop is not a click.
    // Do not capture small intentional drags with a screen-space threshold.
    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
        !std::isfinite(point.thick) || point.x != first.x || point.y != first.y)
      return false;
    // A stationary tablet tap may end with zero pressure. Retain its peak
    // thickness instead of making the release sample the stamp's size.
    thickness = std::max(thickness, point.thick);
  }
  if (thickness <= 0.0 || first.x + 0.25 == first.x) return false;

  for (size_t i = 0; i < carrier.size(); ++i)
    carrier[i] = TThickPoint(first.x + 0.25 * i, first.y, thickness);
  return true;
}

}  // namespace TrailStamp
