#pragma once

#include <algorithm>
#include <cmath>

namespace ThresholdMaskFxUtils {

inline float clamp01(float value) {
  return std::max(0.0f, std::min(1.0f, value));
}

inline float smoothThreshold(float value, float threshold, float softness) {
  value     = clamp01(value);
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

}  // namespace ThresholdMaskFxUtils
