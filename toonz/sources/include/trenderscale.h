#pragma once

#ifndef T_RENDER_SCALE_INCLUDED
#define T_RENDER_SCALE_INCLUDED

#include "tgeometry.h"

#include <limits>
#include <stdexcept>

// The scene stores an integer Shrink: n > 0 divides by n, n < 0 multiplies
// by abs(n). Zero is treated as 1, including when reading old scene files.
// Convert at render boundaries; image readers and individual FX still receive
// their existing positive shrink values (TRenderer resets FX shrink to 1).
namespace TRenderScale {

inline double factor(int shrink) {
  return shrink < 0 ? -double(shrink) : shrink > 0 ? 1.0 / shrink : 1.0;
}

inline double divisor(int shrink) {
  return shrink < 0 ? -1.0 / shrink : shrink > 0 ? double(shrink) : 1.0;
}

inline double length(int pixels, int shrink) {
  const double result =
      shrink < 0 ? pixels * factor(shrink) : pixels / divisor(shrink);
  if (result > (std::numeric_limits<int>::max)() ||
      result < (std::numeric_limits<int>::min)())
    throw std::length_error(
        "Enlarged render dimension exceeds the supported size");
  return result;
}

inline int pixels(int size, int shrink) {
  return static_cast<int>(length(size, shrink));
}

// Keep the unscaled camera's lower-left corner fixed. Render ports use that
// origin and change the extent; scaling about the center would crop the frame.
inline TAffine cameraAffine(const TDimension &resolution, int shrink) {
  const double scale  = factor(shrink);
  const double offset = 0.5 * (scale - 1.0);
  return TTranslation(offset * resolution.lx, offset * resolution.ly) *
         TScale(scale);
}

}  // namespace TRenderScale

#endif
