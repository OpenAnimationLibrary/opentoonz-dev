#include "../thresholdmaskfx_utils.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

bool near(float a, float b, float eps = 1e-6f) {
  return std::fabs(a - b) <= eps;
}

}  // namespace

int main() {
  using ThresholdMaskFxUtils::applyInvert;
  using ThresholdMaskFxUtils::boxBlur;
  using ThresholdMaskFxUtils::smoothThreshold;

  assert(near(smoothThreshold(0.49f, 0.5f, 0.0f), 0.0f));
  assert(near(smoothThreshold(0.50f, 0.5f, 0.0f), 1.0f));
  assert(near(smoothThreshold(0.25f, 0.5f, 0.5f), 0.0f));
  assert(near(smoothThreshold(0.50f, 0.5f, 0.5f), 0.5f));
  assert(near(smoothThreshold(0.75f, 0.5f, 0.5f), 1.0f));
  assert(near(applyInvert(0.2f, false), 0.2f));
  assert(near(applyInvert(0.2f, true), 0.8f));

  std::vector<float> constant(25, 0.4f);
  boxBlur(constant, 5, 5, 1);
  for (float value : constant) assert(near(value, 0.4f));

  std::vector<float> impulse(25, 0.0f);
  impulse[2 * 5 + 2] = 1.0f;
  boxBlur(impulse, 5, 5, 1);
  assert(impulse[2 * 5 + 2] > 0.0f && impulse[2 * 5 + 2] < 1.0f);
  assert(impulse[2 * 5 + 1] > 0.0f);
  assert(impulse[1 * 5 + 2] > 0.0f);

  std::cout << "thresholdmaskfx stage 2 tests passed\n";
  return 0;
}
