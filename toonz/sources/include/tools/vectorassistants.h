#pragma once

#ifndef VECTORASSISTANTS_INCLUDED
#define VECTORASSISTANTS_INCLUDED

#include "tgeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// Geometry in destination raster pixels; no viewer or application state.
namespace VectorAssistants {

inline double length(const TPointD &p) { return std::hypot(p.x, p.y); }

inline double smoothstep(double a, double b, double x) {
  double t = std::max(0.0, std::min(1.0, (x - a) / (b - a)));
  return t * t * (3.0 - 2.0 * t);
}

struct Guide {
  std::vector<TPointD> points;
  std::vector<double> lengths;
  bool closed = false;

  void add(const TPointD &p) {
    double step = points.empty() ? 0.0 : length(p - points.back());
    if (!points.empty() && step < 1e-8) return;
    lengths.push_back(points.empty() ? 0.0 : lengths.back() + step);
    points.push_back(p);
  }

  double totalLength() const { return lengths.empty() ? 0.0 : lengths.back(); }
  bool valid() const { return points.size() > 1 && totalLength() > 1e-8; }
};

// A quadratic's deviation from its chord is at most |p0 - 2*p1 + p2| / 4.
inline bool appendQuadratic(Guide &guide, const TPointD &p0, const TPointD &p1,
                            const TPointD &p2, int depth = 0) {
  if (guide.points.size() >= 16384) return false;
  if (depth >= 16 || length(p0 - p1 * 2.0 + p2) <= 1.0) {
    guide.add(p2);
    return true;
  }
  TPointD a = (p0 + p1) * 0.5, b = (p1 + p2) * 0.5;
  TPointD mid = (a + b) * 0.5;
  return appendQuadratic(guide, p0, a, mid, depth + 1) &&
         appendQuadratic(guide, mid, b, p2, depth + 1);
}

struct Projection {
  TPointD point, tangent;
  double arc      = 0.0;
  double distance = std::numeric_limits<double>::infinity();
};

// Project only onto the requested arc interval, including a closed curve's
// seam. A global nearest-point search here would jump at a self-intersection.
inline Projection project(const Guide &guide, const TPointD &p, double lo,
                          double hi) {
  Projection best;
  const double total = guide.totalLength();
  if (!guide.valid() || hi < lo) return best;
  double firstTurn = guide.closed ? std::floor(lo / total) : 0.0;
  double lastTurn  = guide.closed ? std::floor(hi / total) : 0.0;
  for (double turn = firstTurn; turn <= lastTurn; turn += 1.0) {
    double offset = turn * total;
    double a      = std::max(0.0, lo - offset);
    double b      = std::min(total, hi - offset);
    if (b < a) continue;
    auto begin =
        std::upper_bound(guide.lengths.begin(), guide.lengths.end(), a);
    size_t i = std::max<size_t>(1, begin - guide.lengths.begin());
    for (; i < guide.points.size() && guide.lengths[i - 1] <= b; ++i) {
      const TPointD &p0 = guide.points[i - 1];
      TPointD delta     = guide.points[i] - p0;
      double span       = guide.lengths[i] - guide.lengths[i - 1];
      double t          = ((p - p0) * delta) / (span * span);
      t                 = std::max(
                          std::max(0.0, (a - guide.lengths[i - 1]) / span),
                          std::min(std::min(1.0, (b - guide.lengths[i - 1]) / span), t));
      TPointD q       = p0 + delta * t;
      double distance = length(p - q);
      if (distance < best.distance) {
        best.point    = q;
        best.tangent  = delta * (1.0 / span);
        best.arc      = offset + guide.lengths[i - 1] + t * span;
        best.distance = distance;
      }
    }
  }
  return best;
}

inline int chooseGuide(const std::vector<Guide> &guides, const TPointD &start,
                       const TPointD &direction, double radius) {
  int chosen             = -1;
  double best            = std::numeric_limits<double>::infinity();
  double directionLength = length(direction);
  for (size_t i = 0; i < guides.size(); ++i) {
    Projection q = project(guides[i], start, 0.0, guides[i].totalLength());
    if (q.distance > radius) continue;
    double alignment = directionLength > 1e-8
                           ? std::abs((direction * q.tangent) / directionLength)
                           : 1.0;
    double score     = q.distance + 0.25 * radius * (1.0 - alignment);
    if (score < best) {
      best   = score;
      chosen = static_cast<int>(i);
    }
  }
  return chosen;
}

// Store a state after each input point so an unfinished smoothing tail can be
// replayed without changing the already committed prefix or accumulating drift.
struct State {
  bool started    = false;
  bool released   = false;
  double arc      = 0.0;
  double travel   = 0.0;
  double distance = 0.0;
  double gain     = 0.0;
  TPointD previous, correction;
};

inline TPointD follow(const Guide &guide, State &state, const TPointD &p,
                      double radius = 32.0, double influence = 0.9) {
  if (!guide.valid() || radius <= 0.0 || influence <= 0.0) return p;
  influence = std::min(1.0, influence);
  if (!state.started) {
    Projection q   = project(guide, p, 0.0, guide.totalLength());
    state.started  = true;
    state.previous = p;
    state.arc      = q.arc;
    state.distance = q.distance;
    state.released = q.distance > radius;
    return p;
  }

  TPointD from    = state.previous;
  double distance = length(p - from);
  if (distance < 1e-8) return p + state.correction;
  // Spatial substeps keep capture/release independent of tablet event spacing.
  // Bound work for exceptional pointer jumps without searching a different
  // rail.
  int steps = static_cast<int>(std::ceil(std::min(1024.0, distance)));
  double ds = distance / steps;
  if (ds > radius) state.released = true;
  for (int i = 1; i <= steps; ++i) {
    TPointD pen = from + (p - from) * (static_cast<double>(i) / steps);
    state.travel += ds;
    TPointD target;
    if (!state.released) {
      double window = std::max(2.0, 2.0 * ds);
      if (guide.closed) window = std::min(window, 0.45 * guide.totalLength());
      Projection q =
          project(guide, pen, state.arc - window, state.arc + window);
      if (!std::isfinite(q.distance)) {
        state.released = true;
      } else {
        state.arc = q.arc;
        state.distance +=
            (q.distance - state.distance) * (1.0 - std::exp(-ds / 6.0));
        state.released = state.distance > radius * 1.12;
        double gain    = influence *
                      (1.0 - smoothstep(0.5 * radius, radius, state.distance)) *
                      smoothstep(0.0, 6.0, state.travel);
        // Beyond an open end, fade while allowing the pen to continue forward.
        if (!guide.closed) {
          double along     = (pen - q.point) * q.tangent;
          double overshoot = q.arc <= 1e-8                         ? -along
                             : q.arc >= guide.totalLength() - 1e-8 ? along
                                                                   : 0.0;
          gain *= 1.0 - smoothstep(0.0, radius * 0.5, overshoot);
        }
        state.gain += (gain - state.gain) * (1.0 - std::exp(-ds / 6.0));
        TPointD normal(-q.tangent.y, q.tangent.x);
        target = normal * (((q.point - pen) * normal) * state.gain);
      }
    }
    if (state.released) target = state.correction * std::exp(-ds / 12.0);
    TPointD change      = target - state.correction;
    double changeLength = length(change);
    if (changeLength > 2.0 * ds) change = change * (2.0 * ds / changeLength);
    state.correction += change;
  }
  state.previous = p;
  return p + state.correction;
}

}  // namespace VectorAssistants

#endif
