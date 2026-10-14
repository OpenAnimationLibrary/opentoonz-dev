#include "lut_fit.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void expectFit(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void testIdentityPairFit() {
  otlut::Image source;
  source.width  = 2;
  source.height = 2;
  source.rgb    = {
         0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f,
  };
  const otlut::Image target = source;

  otlut::FitReport report;
  const otlut::Lut3D lut =
      otlut::fitLutFromImagePair(source, target, 3, &report);

  expectFit(lut.size == 3, "fit LUT size");
  expectFit(report.pixelsExamined == 4, "fit pixel count");
  expectFit(report.directlySampledCells > 0, "fit coverage");
  expectFit(
      report.directlySampledCells + report.inferredCells == report.totalCells,
      "all lattice cells should be direct or inferred");
}

void testSparseFillPropagatesGrade() {
  otlut::Image source;
  source.width  = 1;
  source.height = 1;
  source.rgb    = {0.5f, 0.5f, 0.5f};

  otlut::Image target;
  target.width  = 1;
  target.height = 1;
  target.rgb    = {0.6f, 0.55f, 0.45f};

  otlut::FitReport report;
  const otlut::Lut3D lut =
      otlut::fitLutFromImagePair(source, target, 3, &report);

  expectFit(report.directlySampledCells == 1,
            "one exact midpoint should directly sample one cell");
  expectFit(report.inferredCells == 26,
            "remaining 3x3x3 cells should be inferred");

  // White should carry the propagated warm/magenta grade rather than
  // remaining a pure identity endpoint.
  const size_t white = 26 * 3;
  expectFit(std::fabs(lut.rgb[white + 2] - 0.95f) < 0.02f,
            "unsupported white cell should inherit blue-channel offset");
}

void testMismatchedDimensions() {
  otlut::Image a;
  a.width  = 1;
  a.height = 1;
  a.rgb    = {0.0f, 0.0f, 0.0f};

  otlut::Image b = a;
  b.width        = 2;

  bool threw = false;
  try {
    (void)otlut::fitLutFromImagePair(a, b, 3);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expectFit(threw, "mismatched dimensions should fail");
}

}  // namespace

void runFitTests() {
  testIdentityPairFit();
  testSparseFillPropagatesGrade();
  testMismatchedDimensions();
}
