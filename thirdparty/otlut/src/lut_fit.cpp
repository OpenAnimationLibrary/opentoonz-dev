#include "lut_fit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace otlut {
namespace {

size_t index3d(int r, int g, int b, int size) {
  return (static_cast<size_t>(r) * size * size + static_cast<size_t>(g) * size +
          b);
}

void decodeIndex(size_t cell, int size, int& r, int& g, int& b) {
  const size_t plane = static_cast<size_t>(size) * size;
  r                  = static_cast<int>(cell / plane);
  const size_t rem   = cell % plane;
  g                  = static_cast<int>(rem / size);
  b                  = static_cast<int>(rem % size);
}

template <typename Fn>
void forEachNeighbor(int r, int g, int b, int size, Fn&& fn) {
  if (r > 0) fn(index3d(r - 1, g, b, size));
  if (r + 1 < size) fn(index3d(r + 1, g, b, size));
  if (g > 0) fn(index3d(r, g - 1, b, size));
  if (g + 1 < size) fn(index3d(r, g + 1, b, size));
  if (b > 0) fn(index3d(r, g, b - 1, size));
  if (b + 1 < size) fn(index3d(r, g, b + 1, size));
}

void fillUnsupportedCells(Lut3D& lut, const std::vector<double>& weights,
                          size_t sampled) {
  const size_t cells = static_cast<size_t>(lut.size) * lut.size * lut.size;
  if (sampled == 0 || sampled == cells) return;

  const Lut3D identity = makeIdentityLut(lut.size);
  std::vector<float> delta(cells * 3, 0.0f);
  std::vector<unsigned char> known(cells, 0);

  for (size_t cell = 0; cell < cells; ++cell) {
    if (weights[cell] <= 0.0) continue;
    known[cell] = 1;
    for (int channel = 0; channel < 3; ++channel) {
      delta[cell * 3 + channel] =
          lut.rgb[cell * 3 + channel] - identity.rgb[cell * 3 + channel];
    }
  }

  size_t knownCount = sampled;
  std::vector<float> nextDelta(cells * 3, 0.0f);
  std::vector<size_t> frontier;

  // Expand measured color offsets through the lattice. Each newly reached
  // cell receives the average offset of its already-known 6-connected
  // neighbors. Measured cells never move.
  while (knownCount < cells) {
    frontier.clear();

    for (size_t cell = 0; cell < cells; ++cell) {
      if (known[cell]) continue;

      int r = 0, g = 0, b = 0;
      decodeIndex(cell, lut.size, r, g, b);

      std::array<double, 3> sum = {0.0, 0.0, 0.0};
      int count                 = 0;
      forEachNeighbor(r, g, b, lut.size, [&](size_t neighbor) {
        if (!known[neighbor]) return;
        for (int channel = 0; channel < 3; ++channel) {
          sum[channel] += delta[neighbor * 3 + channel];
        }
        ++count;
      });

      if (count == 0) continue;

      for (int channel = 0; channel < 3; ++channel) {
        nextDelta[cell * 3 + channel] =
            static_cast<float>(sum[channel] / count);
      }
      frontier.push_back(cell);
    }

    if (frontier.empty()) break;

    for (size_t cell : frontier) {
      known[cell] = 1;
      for (int channel = 0; channel < 3; ++channel) {
        delta[cell * 3 + channel] = nextDelta[cell * 3 + channel];
      }
    }
    knownCount += frontier.size();
  }

  // Smooth only inferred cells. Directly sampled cells remain hard anchors.
  // This reduces visible wavefront boundaries while preserving evidence.
  constexpr int kSmoothingIterations = 12;
  std::vector<float> smoothed        = delta;

  for (int iteration = 0; iteration < kSmoothingIterations; ++iteration) {
    smoothed = delta;

    for (size_t cell = 0; cell < cells; ++cell) {
      if (weights[cell] > 0.0 || !known[cell]) continue;

      int r = 0, g = 0, b = 0;
      decodeIndex(cell, lut.size, r, g, b);

      std::array<double, 3> sum = {0.0, 0.0, 0.0};
      int count                 = 0;
      forEachNeighbor(r, g, b, lut.size, [&](size_t neighbor) {
        if (!known[neighbor]) return;
        for (int channel = 0; channel < 3; ++channel) {
          sum[channel] += delta[neighbor * 3 + channel];
        }
        ++count;
      });

      if (count == 0) continue;
      for (int channel = 0; channel < 3; ++channel) {
        smoothed[cell * 3 + channel] = static_cast<float>(sum[channel] / count);
      }
    }

    delta.swap(smoothed);
  }

  for (size_t cell = 0; cell < cells; ++cell) {
    if (!known[cell] || weights[cell] > 0.0) continue;
    for (int channel = 0; channel < 3; ++channel) {
      lut.rgb[cell * 3 + channel] = std::clamp(
          identity.rgb[cell * 3 + channel] + delta[cell * 3 + channel], 0.0f,
          1.0f);
    }
  }
}

}  // namespace

double FitReport::coveragePercent() const {
  if (totalCells == 0) return 0.0;
  return 100.0 * static_cast<double>(directlySampledCells) /
         static_cast<double>(totalCells);
}

double FitReport::inferredPercent() const {
  if (totalCells == 0) return 0.0;
  return 100.0 * static_cast<double>(inferredCells) /
         static_cast<double>(totalCells);
}

Lut3D makeIdentityLut(int size) {
  if (size < 2 || size > 129) {
    throw std::invalid_argument("LUT size must be between 2 and 129.");
  }

  Lut3D lut;
  lut.size = size;
  lut.rgb.resize(static_cast<size_t>(size) * size * size * 3);

  const float denom = static_cast<float>(size - 1);
  for (int r = 0; r < size; ++r) {
    for (int g = 0; g < size; ++g) {
      for (int b = 0; b < size; ++b) {
        const size_t cell     = index3d(r, g, b, size);
        lut.rgb[cell * 3 + 0] = r / denom;
        lut.rgb[cell * 3 + 1] = g / denom;
        lut.rgb[cell * 3 + 2] = b / denom;
      }
    }
  }
  return lut;
}

Lut3D fitLutFromImagePair(const Image& source, const Image& target, int size,
                          FitReport* report) {
  if (source.width != target.width || source.height != target.height) {
    throw std::invalid_argument(
        "Source and target images must have identical dimensions.");
  }
  if (source.width <= 0 || source.height <= 0 ||
      source.rgb.size() != target.rgb.size()) {
    throw std::invalid_argument("Source and target images are invalid.");
  }

  Lut3D lut          = makeIdentityLut(size);
  const size_t cells = static_cast<size_t>(size) * size * size;

  std::vector<double> sums(cells * 3, 0.0);
  std::vector<double> weights(cells, 0.0);

  const float scale       = static_cast<float>(size - 1);
  const size_t pixelCount = static_cast<size_t>(source.width) * source.height;

  for (size_t p = 0; p < pixelCount; ++p) {
    const float sr = std::clamp(source.rgb[p * 3 + 0], 0.0f, 1.0f);
    const float sg = std::clamp(source.rgb[p * 3 + 1], 0.0f, 1.0f);
    const float sb = std::clamp(source.rgb[p * 3 + 2], 0.0f, 1.0f);

    const float tr = std::clamp(target.rgb[p * 3 + 0], 0.0f, 1.0f);
    const float tg = std::clamp(target.rgb[p * 3 + 1], 0.0f, 1.0f);
    const float tb = std::clamp(target.rgb[p * 3 + 2], 0.0f, 1.0f);

    const float fr = sr * scale;
    const float fg = sg * scale;
    const float fb = sb * scale;

    const int r0 = static_cast<int>(std::floor(fr));
    const int g0 = static_cast<int>(std::floor(fg));
    const int b0 = static_cast<int>(std::floor(fb));
    const int r1 = std::min(r0 + 1, size - 1);
    const int g1 = std::min(g0 + 1, size - 1);
    const int b1 = std::min(b0 + 1, size - 1);

    const float ar = fr - r0;
    const float ag = fg - g0;
    const float ab = fb - b0;

    for (int ri = 0; ri < 2; ++ri) {
      const int r     = ri ? r1 : r0;
      const double wr = ri ? ar : (1.0 - ar);
      for (int gi = 0; gi < 2; ++gi) {
        const int g     = gi ? g1 : g0;
        const double wg = gi ? ag : (1.0 - ag);
        for (int bi = 0; bi < 2; ++bi) {
          const int b     = bi ? b1 : b0;
          const double wb = bi ? ab : (1.0 - ab);
          const double w  = wr * wg * wb;
          if (w <= 0.0) continue;

          const size_t cell = index3d(r, g, b, size);
          weights[cell] += w;
          sums[cell * 3 + 0] += tr * w;
          sums[cell * 3 + 1] += tg * w;
          sums[cell * 3 + 2] += tb * w;
        }
      }
    }
  }

  size_t sampled = 0;
  for (size_t cell = 0; cell < cells; ++cell) {
    if (weights[cell] <= 0.0) continue;
    ++sampled;
    lut.rgb[cell * 3 + 0] =
        static_cast<float>(sums[cell * 3 + 0] / weights[cell]);
    lut.rgb[cell * 3 + 1] =
        static_cast<float>(sums[cell * 3 + 1] / weights[cell]);
    lut.rgb[cell * 3 + 2] =
        static_cast<float>(sums[cell * 3 + 2] / weights[cell]);
  }

  fillUnsupportedCells(lut, weights, sampled);

  if (report) {
    report->pixelsExamined       = pixelCount;
    report->directlySampledCells = sampled;
    report->inferredCells        = sampled > 0 ? cells - sampled : 0;
    report->totalCells           = cells;
  }

  return lut;
}

}  // namespace otlut
