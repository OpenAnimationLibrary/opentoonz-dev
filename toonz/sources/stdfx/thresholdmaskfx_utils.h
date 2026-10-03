#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace ThresholdMaskFxUtils {

inline float clamp01(float value) {
  return std::max(0.0f, std::min(1.0f, value));
}

inline float smoothThreshold(float value, float threshold, float softness) {
  threshold = clamp01(threshold);
  softness  = clamp01(softness);

  if (softness <= 0.0f) return value >= threshold ? 1.0f : 0.0f;

  const float halfWidth = softness * 0.5f;
  const float low       = threshold - halfWidth;
  const float high      = threshold + halfWidth;

  if (value <= low) return 0.0f;
  if (value >= high) return 1.0f;

  const float width = high - low;
  if (width <= 0.0f) return value >= threshold ? 1.0f : 0.0f;

  const float t = clamp01((value - low) / width);
  return t * t * (3.0f - 2.0f * t);
}

inline float applyInvert(float matte, bool invert) {
  matte = clamp01(matte);
  return invert ? 1.0f - matte : matte;
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

}  // namespace ThresholdMaskFxUtils
