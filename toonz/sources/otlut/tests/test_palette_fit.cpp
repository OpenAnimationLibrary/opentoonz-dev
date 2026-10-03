#include "palette_fit.h"
#include "lut_writer.h"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void expect(const otlut::Lut3D &lut, const std::array<float, 3> &source,
            const std::array<float, 3> &target) {
  const auto actual = otlut::sampleLut(lut, source);
  for (int c = 0; c < 3; ++c)
    check(std::fabs(actual[c] - target[c]) <= 0.251f / 255, "color constraint");
}
}  // namespace

void runPaletteTests() {
  using otlut::ColorPair;
  std::vector<ColorPair> example = {
      {{0, 0, 0}, {0, 0, 0}, 0.1f, "black"},
      {{1, 0, 0}, {9 / 255.0f, 1, 0}, 0.1f, "red to green"}};
  otlut::PaletteFitReport report;
  auto lut = otlut::fitLutFromColorPairs(example, 33, &report);
  check(report.changedColors == 1 && report.preservedColors == 1,
        "example counts");
  for (const auto &p : example) expect(lut, p.source, p.target);
  expect(lut, {0, 0, 1}, {0, 0, 1});
  expect(lut, {1, 1, 1}, {1, 1, 1});

  auto nearby = example;
  nearby.push_back({{0.98f, 0.04f, 0}, {0.98f, 0.04f, 0}, 0.1f, "protected"});
  // Strong neighboring mappings can be unrepresentable at 33 points; never
  // silently accept an inaccurate export. A wider-spaced protected shade works.
  nearby.back().source = nearby.back().target = {0.85f, 0.1f, 0};
  lut = otlut::fitLutFromColorPairs(nearby, 65);
  for (const auto &p : nearby) expect(lut, p.source, p.target);

  const auto broad =
      otlut::fitLutFromColorPairs({{{1, 0, 0}, {0, 1, 0}, 0.3f, "red"}}, 33);
  const auto narrow =
      otlut::fitLutFromColorPairs({{{1, 0, 0}, {0, 1, 0}, 0, "red"}}, 33);
  check(otlut::sampleLut(broad, {0.9f, 0, 0})[1] > 0.1f,
        "tolerance reaches neighbor");
  expect(narrow, {0.9f, 0, 0}, {0.9f, 0, 0});
  expect(broad, {0.5f, 0, 0}, {0.5f, 0, 0});

  bool conflict = false;
  try {
    auto bad = example;
    bad.push_back({{1, 0, 0}, {1, 0, 0}, 0.1f, "protected red"});
    otlut::fitLutFromColorPairs(bad, 33);
  } catch (const std::invalid_argument &) {
    conflict = true;
  }
  check(conflict, "duplicate conflict must fail");
  conflict = false;
  try {
    otlut::fitLutFromColorPairs({{{0.51f, 0.5f, 0.5f}, {0, 0, 0}, 0.1f, "a"},
                                 {{0.5101f, 0.5f, 0.5f}, {1, 1, 1}, 0.1f, "b"}},
                                33);
  } catch (const std::runtime_error &) {
    conflict = true;
  }
  check(conflict, "unresolvable colors must fail");
  bool cancelled = false;
  try {
    otlut::fitLutFromColorPairs(example, 33, nullptr, [] { return true; });
  } catch (const std::runtime_error &) {
    cancelled = true;
  }
  check(cancelled, "cancellation");

  // .cube ordering regression: red must be the first varying input axis.
  std::ostringstream cube;
  otlut::writeCube(cube, otlut::makeIdentityLut(2));
  check(cube.str().find("0.000000000 0.000000000 0.000000000\n"
                        "1.000000000 0.000000000 0.000000000\n") !=
            std::string::npos,
        "cube must be red-fastest");
}
