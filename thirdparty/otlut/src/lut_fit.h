#pragma once

#include "image_loader.h"

#include <cstddef>
#include <vector>

namespace otlut {

struct Lut3D {
  int size = 33;
  std::vector<float> rgb;
};

struct FitReport {
  std::size_t pixelsExamined       = 0;
  std::size_t directlySampledCells = 0;
  std::size_t inferredCells        = 0;
  std::size_t totalCells           = 0;

  double coveragePercent() const;
  double inferredPercent() const;
};

Lut3D makeIdentityLut(int size);
Lut3D fitLutFromImagePair(const Image& source, const Image& target, int size,
                          FitReport* report = nullptr);

}  // namespace otlut
