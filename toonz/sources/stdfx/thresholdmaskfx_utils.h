#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

namespace ThresholdMaskFxUtils {

struct IntRect {
  int x0 = 0;
  int y0 = 0;
  int x1 = -1;
  int y1 = -1;

  bool valid() const { return x0 <= x1 && y0 <= y1; }
  bool contains(int x, int y) const {
    return valid() && x >= x0 && x <= x1 && y >= y0 && y <= y1;
  }
};

inline float clamp01(float value) {
  return std::max(0.0f, std::min(1.0f, value));
}

inline float smoothThreshold(float value, float threshold, float softness) {
  threshold = clamp01(threshold);
  softness  = clamp01(softness);

  if (softness <= 0.0f) return value > threshold ? 1.0f : 0.0f;

  const float halfWidth = softness * 0.5f;
  const float low       = threshold - halfWidth;
  const float high      = threshold + halfWidth;

  if (value <= low) return 0.0f;
  if (value >= high) return 1.0f;

  const float width = high - low;
  if (width <= 0.0f) return value > threshold ? 1.0f : 0.0f;

  const float t = clamp01((value - low) / width);
  return t * t * (3.0f - 2.0f * t);
}

inline float applyInvert(float matte, bool invert) {
  matte = clamp01(matte);
  return invert ? 1.0f - matte : matte;
}

enum ConfidenceCombineMode {
  ConfidencePrimary = 0,
  ConfidenceSecondary,
  ConfidenceIntersect,
  ConfidenceUnion
};

inline float combineConfidence(float primary, float secondary, int mode) {
  primary   = clamp01(primary);
  secondary = clamp01(secondary);
  switch (mode) {
  case ConfidenceSecondary:
    return secondary;
  case ConfidenceIntersect:
    return primary * secondary;
  case ConfidenceUnion:
    return std::max(primary, secondary);
  case ConfidencePrimary:
  default:
    return primary;
  }
}

inline void boxBlur(std::vector<float> &values, int width, int height,
                    int radius) {
  if (radius <= 0 || width <= 0 || height <= 0 || values.empty()) return;

  std::vector<float> temp(values.size(), 0.0f);
  const int diameter = radius * 2 + 1;

  for (int y = 0; y < height; ++y) {
    float sum = 0.0f;
    for (int k = -radius; k <= radius; ++k) {
      const int x = std::max(0, std::min(width - 1, k));
      sum += values[y * width + x];
    }
    for (int x = 0; x < width; ++x) {
      temp[y * width + x] = sum / static_cast<float>(diameter);
      const int removeX = std::max(0, std::min(width - 1, x - radius));
      const int addX = std::max(0, std::min(width - 1, x + radius + 1));
      sum += values[y * width + addX] - values[y * width + removeX];
    }
  }

  for (int x = 0; x < width; ++x) {
    float sum = 0.0f;
    for (int k = -radius; k <= radius; ++k) {
      const int y = std::max(0, std::min(height - 1, k));
      sum += temp[y * width + x];
    }
    for (int y = 0; y < height; ++y) {
      values[y * width + x] = sum / static_cast<float>(diameter);
      const int removeY = std::max(0, std::min(height - 1, y - radius));
      const int addY = std::max(0, std::min(height - 1, y + radius + 1));
      sum += temp[addY * width + x] - temp[removeY * width + x];
    }
  }
}

inline void clipOutsideRect(std::vector<float> &values, int width, int height,
                            const IntRect &rect) {
  if (!rect.valid()) return;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (!rect.contains(x, y)) values[y * width + x] = 0.0f;
    }
  }
}

inline void eraseDisk(std::vector<float> &values, int width, int height,
                      int centerX, int centerY, int radius) {
  if (radius < 0 || width <= 0 || height <= 0) return;
  const int r2 = radius * radius;
  const int x0 = std::max(0, centerX - radius);
  const int x1 = std::min(width - 1, centerX + radius);
  const int y0 = std::max(0, centerY - radius);
  const int y1 = std::min(height - 1, centerY + radius);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const int dx = x - centerX;
      const int dy = y - centerY;
      if (dx * dx + dy * dy <= r2) values[y * width + x] = 0.0f;
    }
  }
}

inline bool findNearestActive(const std::vector<float> &values, int width,
                              int height, int seedX, int seedY,
                              int searchRadius, int &foundX, int &foundY) {
  if (width <= 0 || height <= 0 || values.empty()) return false;
  searchRadius = std::max(0, searchRadius);

  float bestDistance2 = -1.0f;
  const int x0 = std::max(0, seedX - searchRadius);
  const int x1 = std::min(width - 1, seedX + searchRadius);
  const int y0 = std::max(0, seedY - searchRadius);
  const int y1 = std::min(height - 1, seedY + searchRadius);

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      if (values[y * width + x] <= 1e-6f) continue;
      const float dx = static_cast<float>(x - seedX);
      const float dy = static_cast<float>(y - seedY);
      const float distance2 = dx * dx + dy * dy;
      if (distance2 > searchRadius * searchRadius) continue;
      if (bestDistance2 < 0.0f || distance2 < bestDistance2) {
        bestDistance2 = distance2;
        foundX        = x;
        foundY        = y;
      }
    }
  }
  return bestDistance2 >= 0.0f;
}

inline bool keepConnectedComponent(std::vector<float> &values, int width,
                                   int height, int seedX, int seedY,
                                   int searchRadius, bool eightConnected) {
  int startX = 0;
  int startY = 0;
  if (!findNearestActive(values, width, height, seedX, seedY, searchRadius,
                         startX, startY)) {
    std::fill(values.begin(), values.end(), 0.0f);
    return false;
  }

  std::vector<uint8_t> keep(values.size(), 0);
  std::queue<int> pending;
  const int start = startY * width + startX;
  keep[start]     = 1;
  pending.push(start);

  const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
  const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
  const int neighborCount = eightConnected ? 8 : 4;

  while (!pending.empty()) {
    const int index = pending.front();
    pending.pop();
    const int x = index % width;
    const int y = index / width;

    for (int i = 0; i < neighborCount; ++i) {
      const int nx = x + dx[i];
      const int ny = y + dy[i];
      if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
      const int n = ny * width + nx;
      if (keep[n] || values[n] <= 1e-6f) continue;
      keep[n] = 1;
      pending.push(n);
    }
  }

  for (size_t i = 0; i < values.size(); ++i) {
    if (!keep[i]) values[i] = 0.0f;
  }
  return true;
}

}  // namespace ThresholdMaskFxUtils
