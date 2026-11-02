#pragma once

#include <algorithm>
#include <cmath>

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
};
