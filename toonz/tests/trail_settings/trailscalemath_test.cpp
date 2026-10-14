#include "trailscale.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void checkScale(double multiplier) {
  require(TrailScale::normalized(multiplier) == multiplier,
          "Valid fractional multiplier was changed");
  require(TrailScale::apply(12.0, multiplier) == 12.0 * multiplier,
          "Scale did not multiply both dimensions");
  const double halfWidth = 320.0, halfHeight = 16.0;
  const double scale = TrailScale::apply(0.5, multiplier);
  const double bound = TrailScale::radius(halfWidth, halfHeight, scale);
  for (int degrees = 0; degrees < 360; ++degrees) {
    const double angle = degrees * std::acos(-1.0) / 180.0;
    for (int sx : {-1, 1}) {
      for (int sy : {-1, 1}) {
        const double x = scale * (sx * halfWidth * std::cos(angle) -
                                  sy * halfHeight * std::sin(angle));
        const double y = scale * (sx * halfWidth * std::sin(angle) +
                                  sy * halfHeight * std::cos(angle));
        require(std::abs(x) <= bound + 1e-10 && std::abs(y) <= bound + 1e-10,
                "Rotated stamp escaped its conservative bounds");
      }
    }
  }
  // The click carrier is one unit; both renderers retain a two-unit minimum.
  for (double gap : {-50.0, 0.0, 50.0}) {
    const double interval = (std::max)(2.0, scale * 2.0 * halfWidth + gap);
    int placements        = 0;
    for (double position = 0.0; position < 1.0; position += interval)
      ++placements;
    require(placements == 1, "A scaled click would make multiple stamps");
  }
}
}  // namespace

int main() try {
  for (double value : {0.25, 0.5, 0.75, 1.0, 1.5, 2.25, 5.0, 10.0})
    checkScale(value);
  require(TrailScale::normalized(0.0) == 0.25 &&
              TrailScale::normalized(-1.0) == 0.25 &&
              TrailScale::normalized(100.0) == 10.0,
          "Finite out-of-range values were not bounded");
  for (double value : {std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()})
    require(TrailScale::normalized(value) == 1.0,
            "Nonfinite metadata did not fall back to 1x");
  require(TrailScale::apply(0.0, 10.0) == 0.0,
          "Multiplier invented a nonzero width");
  std::cout
      << "Trail scale range, fractional sizes, bounds and intervals passed\n";
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
