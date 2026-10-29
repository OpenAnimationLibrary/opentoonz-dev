#pragma once

#include "tgeometry.h"

namespace SelectionRepeat {

inline bool capture(const TPointD &beforeOrigin, const TPointD &beforeX,
                    const TPointD &beforeY, const TPointD &afterOrigin,
                    const TPointD &afterX, const TPointD &afterY,
                    TAffine &affine) {
  const auto frame = [](const TPointD &origin, const TPointD &px,
                        const TPointD &py) {
    const TPointD x = px - origin, y = py - origin;
    return TAffine(x.x, y.x, origin.x, x.y, y.y, origin.y);
  };
  const TAffine before = frame(beforeOrigin, beforeX, beforeY);
  if (std::abs(before.det()) < 1e-8) return false;
  affine = frame(afterOrigin, afterX, afterY) * before.inv();
  return std::isfinite(affine.a11) && std::isfinite(affine.a12) &&
         std::isfinite(affine.a13) && std::isfinite(affine.a21) &&
         std::isfinite(affine.a22) && std::isfinite(affine.a23) &&
         std::abs(affine.det()) >= 1e-8;
}

inline TAffine rebase(const TAffine &affine, const TPointD &anchor,
                      const TPointD &center) {
  const TPointD offset = center - anchor;
  return TTranslation(offset) * affine * TTranslation(-offset);
}

}  // namespace SelectionRepeat
