#include "tnztools/trailstamp.h"

#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void checkClickCarriers() {
  for (double x : {-10000.0, -0.125, 0.0, 12.75, 10000.0}) {
    for (double radius : {0.001, 0.5, 2.5, 500.0}) {
      std::array<TThickPoint, 5> carrier;
      require(TrailStamp::makeClickCarrier({{x, -8.0, radius}}, carrier),
              "Stationary click was not promoted");
      require(carrier.front().x == x && carrier.front().y == -8.0,
              "Stamp moved away from the click");
      require(carrier.back().x - carrier.front().x == 1.0,
              "Carrier is degenerate or can emit multiple stamps");
      for (size_t i = 0; i < carrier.size(); ++i) {
        require(carrier[i].x == x + 0.25 * i && carrier[i].y == -8.0 &&
                    carrier[i].thick == radius,
                "Carrier changed the brush thickness or direction");
        // Current PLI coordinate deltas use 1/16384 stage units. Even the
        // older 1/1000 grid retains every quarter-unit control point step.
        if (i)
          require(int((carrier[i].x - carrier[i - 1].x) * 1000.0) > 0,
                  "Carrier collapsed under coordinate quantization");
      }
      for (double distance : {-50.0, 0.0, 50.0}) {
        const double spacing = std::max(2.0, 2.0 * radius + distance);
        require(carrier.back().x - carrier.front().x < spacing,
                "Carrier reaches a second stamp");
      }
    }
  }
}

void checkTapPressure() {
  std::array<TThickPoint, 5> carrier;
  require(TrailStamp::makeClickCarrier(
              {{4, 7, 0}, {4, 7, 3}, {4, 7, 8}, {4, 7, 0}}, carrier),
          "Zero-pressure release erased a tap");
  for (const auto &point : carrier)
    require(point.thick == 8.0, "Tap did not preserve peak thickness");
}

void checkNonClicks() {
  std::array<TThickPoint, 5> carrier;
  const std::vector<std::vector<TThickPoint>> cases = {
      {},
      {{0, 0, 0}},
      {{0, 0, -1}},
      {{0, 0, 5}, {1e-9, 0, 5}},
      {{0, 0, 5}, {0, 1e-9, 5}},
      {{0, 0, 5}, {10, 0, 5}, {0, 0, 5}},
      {{std::numeric_limits<double>::max(), 0, 5}},
      {{std::numeric_limits<double>::infinity(), 0, 5}},
      {{0, std::numeric_limits<double>::quiet_NaN(), 5}},
      {{0, 0, std::numeric_limits<double>::quiet_NaN()}}};
  for (const auto &points : cases)
    require(!TrailStamp::makeClickCarrier(points, carrier),
            "Empty, invisible, invalid, or moving stroke became a stamp");
}
}  // namespace

int main() {
  checkClickCarriers();
  checkTapPressure();
  checkNonClicks();
}
