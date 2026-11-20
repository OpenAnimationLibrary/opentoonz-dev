#include "trenderscale.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
void near(double actual, double expected) {
  require(std::abs(actual - expected) < 1e-8, "Unexpected camera coordinate");
}

void checkCamera(int shrink, int expectedWidth, int expectedHeight) {
  const TDimension camera(1920, 1080);
  const TAffine affine     = TRenderScale::cameraAffine(camera, shrink);
  const TPointD lowerLeft  = affine * TPointD(-960, -540);
  const TPointD upperRight = affine * TPointD(960, 540);
  near(lowerLeft.x, -960);
  near(lowerLeft.y, -540);
  near(upperRight.x, -960 + expectedWidth);
  near(upperRight.y, -540 + expectedHeight);
  require(affine.det() > 0, "Enlargement must not mirror the artwork");
  require(TRenderScale::pixels(camera.lx, shrink) == expectedWidth,
          "Unexpected render width");
  require(TRenderScale::pixels(camera.ly, shrink) == expectedHeight,
          "Unexpected render height");

  // An off-center subcamera must retain the same composition. Its pixel
  // position relative to the camera origin must match the transformed tree.
  const TPointD source(123, -87);
  const TPointD transformed = affine * source;
  near(transformed.x - lowerLeft.x,
       (source.x + 960) / TRenderScale::divisor(shrink));
  near(transformed.y - lowerLeft.y,
       (source.y + 540) / TRenderScale::divisor(shrink));
  const TPointD restored = affine.inv() * transformed;
  near(restored.x, source.x);
  near(restored.y, source.y);
}
}  // namespace

int main() {
  checkCamera(1, 1920, 1080);
  checkCamera(-1, 1920, 1080);
  checkCamera(0, 1920, 1080);
  checkCamera(2, 960, 540);
  checkCamera(3, 640, 360);
  checkCamera(-2, 3840, 2160);
  checkCamera(-3, 5760, 3240);
  checkCamera(-4, 7680, 4320);
  require(TRenderScale::pixels(1919, 2) == 959,
          "Positive Shrink must retain integer truncation");
  require(TRenderScale::pixels(1919, -3) == 5757,
          "Odd dimensions must enlarge exactly");
  // Subcamera end points are inclusive; scale their exclusive edge first.
  require(TRenderScale::pixels(400 + 1, -3) - 1 == 1202,
          "Inclusive subcamera edge is off by one");
  bool rejected = false;
  try {
    TRenderScale::pixels(1920, (std::numeric_limits<int>::min)());
  } catch (const std::length_error &) {
    rejected = true;
  }
  require(rejected, "Oversized scene values must not overflow dimensions");
  std::cout << "Render dimensions, camera framing, subcamera and overflow "
               "checks passed\n";
}
