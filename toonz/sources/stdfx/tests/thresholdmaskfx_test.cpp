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
  using ThresholdMaskFxUtils::IntRect;
  using ThresholdMaskFxUtils::applyInvert;
  using ThresholdMaskFxUtils::boxBlur;
  using ThresholdMaskFxUtils::combineConfidence;
  using ThresholdMaskFxUtils::clipOutsideRect;
  using ThresholdMaskFxUtils::eraseDisk;
  using ThresholdMaskFxUtils::keepConnectedComponent;
  using ThresholdMaskFxUtils::smoothThreshold;

  assert(near(smoothThreshold(0.49f, 0.5f, 0.0f), 0.0f));
  assert(near(smoothThreshold(0.50f, 0.5f, 0.0f), 1.0f));
  assert(near(smoothThreshold(0.25f, 0.5f, 0.5f), 0.0f));
  assert(near(smoothThreshold(0.50f, 0.5f, 0.5f), 0.5f));
  assert(near(smoothThreshold(0.75f, 0.5f, 0.5f), 1.0f));
  assert(near(applyInvert(0.2f, false), 0.2f));
  assert(near(applyInvert(0.2f, true), 0.8f));
  assert(near(combineConfidence(0.8f, 0.5f,
                                ThresholdMaskFxUtils::ConfidencePrimary),
              0.8f));
  assert(near(combineConfidence(0.8f, 0.5f,
                                ThresholdMaskFxUtils::ConfidenceSecondary),
              0.5f));
  assert(near(combineConfidence(0.8f, 0.5f,
                                ThresholdMaskFxUtils::ConfidenceIntersect),
              0.4f));
  assert(near(combineConfidence(0.8f, 0.5f,
                                ThresholdMaskFxUtils::ConfidenceUnion),
              0.8f));

  std::vector<float> constant(25, 0.4f);
  boxBlur(constant, 5, 5, 1);
  for (float value : constant) assert(near(value, 0.4f));

  std::vector<float> impulse(25, 0.0f);
  impulse[2 * 5 + 2] = 1.0f;
  boxBlur(impulse, 5, 5, 1);
  assert(impulse[2 * 5 + 2] > 0.0f && impulse[2 * 5 + 2] < 1.0f);

  std::vector<float> islands(35, 0.0f);
  islands[1 * 7 + 1] = islands[1 * 7 + 2] = 1.0f;
  islands[2 * 7 + 1] = islands[2 * 7 + 2] = 1.0f;
  islands[1 * 7 + 5] = islands[2 * 7 + 5] = 1.0f;
  assert(keepConnectedComponent(islands, 7, 5, 1, 1, 0, false));
  assert(islands[1 * 7 + 1] == 1.0f);
  assert(islands[1 * 7 + 5] == 0.0f);

  std::vector<float> snap(25, 0.0f);
  snap[2 * 5 + 3] = 1.0f;
  assert(keepConnectedComponent(snap, 5, 5, 2, 2, 2, true));
  assert(snap[2 * 5 + 3] == 1.0f);

  std::vector<float> boxed(25, 1.0f);
  clipOutsideRect(boxed, 5, 5, IntRect{1, 1, 3, 3});
  assert(boxed[0] == 0.0f);
  assert(boxed[2 * 5 + 2] == 1.0f);

  std::vector<float> carved(25, 1.0f);
  eraseDisk(carved, 5, 5, 2, 2, 1);
  assert(carved[2 * 5 + 2] == 0.0f);
  assert(carved[0] == 1.0f);

  std::cout << "thresholdmaskfx stage 4 tests passed\n";
  return 0;
}
