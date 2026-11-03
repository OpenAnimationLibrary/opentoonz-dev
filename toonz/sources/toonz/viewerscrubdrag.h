#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Cache consecutive exposure boundaries, not unique drawing IDs. The caller's
// cell comparison includes level identity and preserves intentional blank runs.
template <typename CellAt>
std::vector<int> viewerScrubDrawingStops(int first, int last, CellAt cellAt) {
  std::vector<int> stops;
  if (first > last) return stops;
  stops.push_back(first);
  auto previous = cellAt(first);
  for (int row = first; row < last;) {
    const auto cell = cellAt(++row);
    if (cell != previous) stops.push_back(row);
    previous = cell;
  }
  return stops;
}

// Relative, screen-space scrubbing. Preserve sub-frame motion between events,
// but discard overshoot at a clamped boundary so reversing responds
// immediately.
class ViewerScrubDrag {
  double m_lastX     = 0.0;
  double m_remainder = 0.0;

public:
  void begin(double x) {
    m_lastX     = x;
    m_remainder = 0.0;
  }

  int move(double x, double pixelsPerFrame, int current, int first, int last,
           bool wrap) {
    m_remainder += x - m_lastX;
    m_lastX          = x;
    const auto steps = static_cast<long long>(m_remainder / pixelsPerFrame);
    if (!steps || first > last) return current;
    m_remainder -= steps * pixelsPerFrame;
    long long target = static_cast<long long>(current) + steps;
    if (wrap) {
      const long long count = static_cast<long long>(last) - first + 1;
      target = first + ((target - first) % count + count) % count;
    } else {
      if (target < first || target > last) m_remainder = 0.0;
      target = std::max<long long>(first, std::min<long long>(last, target));
    }
    return static_cast<int>(target);
  }

  int moveDrawing(double x, double pixelsPerDrawing, int current, int last,
                  bool wrap, const std::vector<int> &stops) {
    if (stops.empty()) return current;
    const int count = static_cast<int>(stops.size());
    const auto next = std::upper_bound(stops.begin(), stops.end(), current);
    const int index =
        current > last ? count : static_cast<int>(next - stops.begin()) - 1;
    const int target = move(x, pixelsPerDrawing, index, 0, count - 1, wrap);
    // Do not snap a click or sub-step movement to the start of a held drawing.
    return target == index ? current : stops[target];
  }
};
