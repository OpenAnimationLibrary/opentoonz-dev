#pragma once

// Auto Fill adapted from matitanimata/ztoryc, toonzrasterbrushtool.cpp.
// Copyright (c) 2025-2026 Franco Bianco / Matitanimata
// Copyright (c) 2026 the OpenToonz contributors
// SPDX-License-Identifier: BSD-3-Clause
// See LICENSE.txt. Unlike the source's stroke-bounding-box heuristic, region
// eligibility below compares actual before/after exterior connectivity.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace RasterAutoFill {

struct Point {
  int x = 0, y = 0;
  Point() = default;
  Point(int x, int y) : x(x), y(y) {}
  bool operator==(const Point &p) const { return x == p.x && y == p.y; }
};

struct StrokeEnd {
  Point first, last;
  double firstRadius = 0.5, lastRadius = 0.5;
};

struct Closure {
  bool found  = false;
  bool hasGap = false;
  Point from, to;
};

using Mask = std::vector<unsigned char>;

inline std::size_t pixelCount(int width, int height) {
  if (width <= 0 || height <= 0) return 0;
  const std::size_t w = static_cast<std::size_t>(width);
  const std::size_t h = static_cast<std::size_t>(height);
  if (w > static_cast<std::size_t>(std::numeric_limits<int>::max()) / h)
    throw std::length_error("Auto Fill analysis area is too large");
  return w * h;
}

template <class Pixel>
bool visibleInk(const Pixel &pixel) {
  // A stored ink index at tone 255 is NOT a visible line.
  return pixel.getInk() != 0 && pixel.getTone() < 255;
}

inline double distanceSquared(Point a, Point b) {
  const double dx = static_cast<double>(a.x) - b.x;
  const double dy = static_cast<double>(a.y) - b.y;
  return dx * dx + dy * dy;
}

// Visit a conservative supercover of the proposed one-pixel Tape-style bridge.
// Testing both side pixels at diagonal steps prevents cutting a foreign-style
// corner. Endpoint bounds must be checked by the caller.
template <class Visit>
bool walkSegment(Point a, Point b, const Visit &visit) {
  int x = a.x, y = a.y;
  const int dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
  const int sx = a.x < b.x ? 1 : -1, sy = a.y < b.y ? 1 : -1;
  int error = dx - dy;
  for (;;) {
    if (!visit(Point(x, y))) return false;
    if (x == b.x && y == b.y) return true;
    const int e2     = 2 * error;
    const bool moveX = e2 > -dy, moveY = e2 < dx;
    if (moveX && moveY &&
        (!visit(Point(x + sx, y)) || !visit(Point(x, y + sy))))
      return false;
    if (moveX) {
      error -= dy;
      x += sx;
    }
    if (moveY) {
      error += dx;
      y += sy;
    }
  }
}

// Candidates are pre-existing same-style ink, plus the new stroke's start cap.
// The just-drawn trailing pixels are deliberately NOT candidates. Starting
// points and adjacent lines compete on distance; the start has no priority.
template <class BeforeAt, class AfterAt>
Closure findClosure(int width, int height, const BeforeAt &beforeAt,
                    const AfterAt &afterAt, const StrokeEnd &stroke, int ink,
                    int maxGap) {
  Closure result;
  if (width <= 0 || height <= 0 || ink <= 0 || maxGap <= 0) return result;
  auto inBounds = [&](Point p) {
    return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height;
  };
  if (!inBounds(stroke.last)) return result;
  const auto origin = afterAt(stroke.last.x, stroke.last.y);
  if (!visibleInk(origin) || origin.getInk() != ink) return result;

  const double radius = std::max(0.5, stroke.lastRadius) + maxGap;
  const int reach     = static_cast<int>(std::ceil(std::min(radius, 2048.0)));
  double best         = static_cast<double>(reach) * reach + 1.0;
  const double cap    = std::max(0.5, stroke.firstRadius) + 0.5;
  for (int y = std::max(0, stroke.last.y - reach);
       y <= std::min(height - 1, stroke.last.y + reach); ++y) {
    for (int x = std::max(0, stroke.last.x - reach);
         x <= std::min(width - 1, stroke.last.x + reach); ++x) {
      const Point p(x, y);
      const double d2 = distanceSquared(p, stroke.last);
      if (d2 >= best || d2 > radius * radius) continue;
      const auto after = afterAt(x, y);
      if (!visibleInk(after) || after.getInk() != ink) continue;
      const auto before   = beforeAt(x, y);
      const bool oldInk   = visibleInk(before) && before.getInk() == ink;
      const bool startCap = distanceSquared(p, stroke.first) <= cap * cap;
      if (!oldInk && !startCap) continue;
      bool gap         = false;
      const bool clear = walkSegment(stroke.last, p, [&](Point q) {
        if (!inBounds(q)) return false;
        const auto pixel = afterAt(q.x, q.y);
        if (visibleInk(pixel) && pixel.getInk() != ink) return false;
        if (!visibleInk(pixel)) gap = true;
        return true;
      });
      if (!clear) continue;
      best          = d2;
      result.found  = true;
      result.hasGap = gap;
      result.from   = stroke.last;
      result.to     = p;
    }
  }
  return result;
}

// The analysis border is always exterior, never an artificial closing edge.
// Four-connected free pixels match a raster boundary without corner leakage.
template <class PixelAt>
Mask exterior(int width, int height, const PixelAt &at) {
  Mask outside(pixelCount(width, height), 0);
  if (outside.empty()) return outside;
  std::vector<int> queue;
  auto enqueue = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const int index = y * width + x;
    if (outside[index] || visibleInk(at(x, y))) return;
    outside[index] = 1;
    queue.push_back(index);
  };
  for (int x = 0; x < width; ++x) {
    enqueue(x, 0);
    enqueue(x, height - 1);
  }
  for (int y = 1; y + 1 < height; ++y) {
    enqueue(0, y);
    enqueue(width - 1, y);
  }
  for (std::size_t i = 0; i < queue.size(); ++i) {
    const int x = queue[i] % width, y = queue[i] / width;
    enqueue(x - 1, y);
    enqueue(x + 1, y);
    enqueue(x, y - 1);
    enqueue(x, y + 1);
  }
  return outside;
}

struct Regions {
  Mask interior;
  std::vector<Point> seeds;
};

template <class BeforeAt, class AfterAt>
Regions newlyEnclosed(int width, int height, const BeforeAt &beforeAt,
                      const AfterAt &afterAt) {
  Regions regions;
  const Mask before = exterior(width, height, beforeAt);
  const Mask after  = exterior(width, height, afterAt);
  regions.interior.resize(before.size(), 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int index  = y * width + x;
      const auto pixel = afterAt(x, y);
      if (before[index] && !after[index] && !visibleInk(pixel) &&
          pixel.getPaint() == 0)
        regions.interior[index] = 1;
    }
  }
  std::vector<int> queue;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int index = y * width + x;
      if (regions.interior[index] != 1) continue;
      regions.seeds.push_back(Point(x, y));
      queue.clear();
      queue.push_back(index);
      regions.interior[index] = 2;
      auto enqueue            = [&](int nx, int ny) {
        if (nx < 0 || ny < 0 || nx >= width || ny >= height) return;
        const int next = ny * width + nx;
        if (regions.interior[next] != 1) return;
        regions.interior[next] = 2;
        queue.push_back(next);
      };
      for (std::size_t i = 0; i < queue.size(); ++i) {
        const int qx = queue[i] % width, qy = queue[i] / width;
        enqueue(qx - 1, qy);
        enqueue(qx + 1, qy);
        enqueue(qx, qy - 1);
        enqueue(qx, qy + 1);
      }
    }
  }
  return regions;
}

// Run the native tone-aware filler on scratch pixels, then check that it did
// not reach background or an unrelated empty region. On a leak the caller
// keeps the stroke/closure but discards the fill, rather than spilling paint.
template <class BeforeAt, class FilledAt>
bool fillStayedInside(int width, int height, const BeforeAt &beforeAt,
                      const FilledAt &filledAt, const Mask &interior) {
  if (interior.size() != pixelCount(width, height)) return false;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto before = beforeAt(x, y);
      if (before.getPaint() == filledAt(x, y).getPaint()) continue;
      if (before.getPaint() != 0) return false;
      if (!visibleInk(before) && !interior[y * width + x]) return false;
    }
  }
  return true;
}

}  // namespace RasterAutoFill
