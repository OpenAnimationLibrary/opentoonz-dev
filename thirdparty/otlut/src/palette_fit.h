#pragma once

#include "lut_fit.h"

#include <array>
#include <functional>
#include <string>

namespace otlut {

struct ColorPair {
  std::array<float, 3> source;
  std::array<float, 3> target;
  // RGB Euclidean radius in normalized (0..1) palette RGB coordinates.
  float tolerance = 0.1f;
  std::string label;
};

struct PaletteFitReport {
  int changedColors   = 0;
  int preservedColors = 0;
  float maximumError  = 0.0f;
};

std::array<float, 3> sampleLut(const Lut3D &lut,
                               const std::array<float, 3> &rgb);

// Sparse artist-authored correspondences use local influence, unlike image
// fitting's whole-volume propagation. Throws on conflicts or unresolved
// anchors.
Lut3D fitLutFromColorPairs(const std::vector<ColorPair> &pairs, int size,
                           PaletteFitReport *report               = nullptr,
                           const std::function<bool()> &cancelled = {});

}  // namespace otlut
