#include "../thresholdmaskfx_utils.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

bool near(float a, float b, float eps = 1e-6f) {
  return std::fabs(a - b) <= eps;
}

}  // namespace

int main() {
  using ThresholdMaskFxUtils::applyInvert;
  using ThresholdMaskFxUtils::smoothThreshold;

  assert(near(smoothThreshold(0.49f, 0.5f, 0.0f), 0.0f));
  assert(near(smoothThreshold(0.50f, 0.5f, 0.0f), 1.0f));
  assert(near(smoothThreshold(0.25f, 0.5f, 0.5f), 0.0f));
  assert(near(smoothThreshold(0.50f, 0.5f, 0.5f), 0.5f));
  assert(near(smoothThreshold(0.75f, 0.5f, 0.5f), 1.0f));
  assert(near(applyInvert(0.2f, false), 0.2f));
  assert(near(applyInvert(0.2f, true), 0.8f));

  std::cout << "thresholdmaskfx stage 1 tests passed\n";
  return 0;
}
