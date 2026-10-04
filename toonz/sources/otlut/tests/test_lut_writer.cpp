#include "lut_writer.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

void runFitTests();
void runPaletteTests();

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> result;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) result.push_back(line);
  return result;
}

void testCubeIdentity() {
  otlut::LutWriteOptions options;
  options.title = "test";

  std::ostringstream output;
  otlut::writeIdentityCube(output, 2, options);
  const auto all = lines(output.str());

  expect(all.size() == 13, "2x2x2 cube should contain 5 headers + 8 entries");
  expect(all[1] == "TITLE \"test\"", "cube title");
  expect(all[2] == "LUT_3D_SIZE 2", "cube size");
  expect(all[5] == "0.000000000 0.000000000 0.000000000", "cube black");
  expect(all[12] == "1.000000000 1.000000000 1.000000000", "cube white");
}

void testThreeDlIdentity() {
  otlut::LutWriteOptions options;
  options.outputBitDepth = 8;

  std::ostringstream output;
  otlut::writeIdentityThreeDl(output, 3, options);
  const auto all = lines(output.str());

  expect(all.size() == 31, "3x3x3 3dl should contain 4 headers + 27 entries");
  expect(all[1] == "3DMESH", "3dl keyword");
  expect(all[2] == "Mesh 1 8", "3dl mesh header");
  expect(all[3] == "0 128 255", "3dl input grid");
  expect(all[4] == "0 0 0", "3dl black");
  expect(all[30] == "255 255 255", "3dl white");
}

void testThreeDlSizes() {
  expect(otlut::isSupportedThreeDlSize(17), "17 should be supported");
  expect(otlut::isSupportedThreeDlSize(33), "33 should be supported");
  expect(otlut::isSupportedThreeDlSize(65), "65 should be supported");
  expect(otlut::isSupportedThreeDlSize(129), "129 should be supported");
  expect(!otlut::isSupportedThreeDlSize(32), "32 should not be supported");
  expect(otlut::threeDlInputBitDepthForSize(33) == 5,
         "33 should map to input bit depth 5");
}

void testFormatDetection() {
  expect(otlut::formatFromPath("look.CUBE") == otlut::LutFormat::Cube,
         "cube extension should be case-insensitive");
  expect(otlut::formatFromPath("look.3dl") == otlut::LutFormat::ThreeDl,
         "3dl extension");
}

}  // namespace

int main() {
  try {
    testCubeIdentity();
    testThreeDlIdentity();
    testThreeDlSizes();
    testFormatDetection();
    runFitTests();
    runPaletteTests();
    std::cout << "All otlut tests passed.\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& e) {
    std::cerr << "Test failure: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
}
