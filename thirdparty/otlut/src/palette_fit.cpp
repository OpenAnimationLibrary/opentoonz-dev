#include "palette_fit.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace otlut {
namespace {
struct Stencil {
  size_t cells[8];
  float weights[8];
};

Stencil stencil(const std::array<float, 3> &rgb, int size) {
  int lo[3];
  float fraction[3];
  for (int c = 0; c < 3; ++c) {
    const float position = std::clamp(rgb[c], 0.0f, 1.0f) * (size - 1);
    lo[c]                = std::min(static_cast<int>(position), size - 2);
    fraction[c]          = position - lo[c];
  }
  Stencil result;
  int i = 0;
  for (int r = 0; r < 2; ++r)
    for (int g = 0; g < 2; ++g)
      for (int b = 0; b < 2; ++b, ++i) {
        result.cells[i] = (static_cast<size_t>(lo[0] + r) * size * size +
                           (lo[1] + g) * size + lo[2] + b) *
                          3;
        result.weights[i] = (r ? fraction[0] : 1 - fraction[0]) *
                            (g ? fraction[1] : 1 - fraction[1]) *
                            (b ? fraction[2] : 1 - fraction[2]);
      }
  return result;
}

std::array<float, 3> sample(const Lut3D &lut, const Stencil &s) {
  std::array<float, 3> out = {0, 0, 0};
  for (int i = 0; i < 8; ++i)
    for (int c = 0; c < 3; ++c)
      out[c] += s.weights[i] * lut.rgb[s.cells[i] + c];
  return out;
}

// Project a channel onto its trilinear constraint within the RGB gamut. When
// a node reaches a bound, redistribute the remaining correction among the
// free nodes. Simply clamping an unconstrained projection can converge very
// slowly when a source sits almost exactly on a grid plane.
void projectChannel(Lut3D &lut, const Stencil &s, int channel, float target) {
  bool free[8];
  for (int i = 0; i < 8; ++i) free[i] = s.weights[i] > 0;
  for (int pass = 0; pass < 8; ++pass) {
    double actual = 0, norm = 0;
    for (int i = 0; i < 8; ++i) {
      actual += s.weights[i] * double(lut.rgb[s.cells[i] + channel]);
      if (free[i]) norm += double(s.weights[i]) * s.weights[i];
    }
    if (norm == 0) return;
    const double correction = (target - actual) / norm;
    bool bounded            = false;
    for (int i = 0; i < 8; ++i) {
      if (!free[i]) continue;
      float &value      = lut.rgb[s.cells[i] + channel];
      const double next = value + s.weights[i] * correction;
      value             = static_cast<float>(std::clamp(next, 0.0, 1.0));
      if (next < 0 || next > 1) {
        free[i] = false;
        bounded = true;
      }
    }
    if (!bounded) return;
  }
}

float distance2(const std::array<float, 3> &a, const std::array<float, 3> &b) {
  float result = 0;
  for (int c = 0; c < 3; ++c) result += (a[c] - b[c]) * (a[c] - b[c]);
  return result;
}

void checkCancelled(const std::function<bool()> &cancelled) {
  if (cancelled && cancelled()) throw std::runtime_error("Cancelled.");
}
}  // namespace

std::array<float, 3> sampleLut(const Lut3D &lut,
                               const std::array<float, 3> &rgb) {
  if (lut.size < 2 ||
      lut.rgb.size() != static_cast<size_t>(lut.size) * lut.size * lut.size * 3)
    throw std::invalid_argument("Invalid LUT grid.");
  return sample(lut, stencil(rgb, lut.size));
}

Lut3D fitLutFromColorPairs(const std::vector<ColorPair> &input, int size,
                           PaletteFitReport *report,
                           const std::function<bool()> &cancelled) {
  Lut3D lut = makeIdentityLut(size);
  std::vector<ColorPair> pairs;
  for (const ColorPair &p : input) {
    checkCancelled(cancelled);
    for (int c = 0; c < 3; ++c)
      if (!std::isfinite(p.source[c]) || !std::isfinite(p.target[c]) ||
          p.source[c] < 0 || p.source[c] > 1 || p.target[c] < 0 ||
          p.target[c] > 1)
        throw std::invalid_argument("Invalid RGB value: " + p.label);
    if (!std::isfinite(p.tolerance) || p.tolerance < 0 || p.tolerance > 1)
      throw std::invalid_argument("Invalid tolerance: " + p.label);
    bool duplicate = false;
    for (ColorPair &existing : pairs)
      if (distance2(p.source, existing.source) < 1e-12f) {
        if (distance2(p.target, existing.target) > 1e-12f)
          throw std::invalid_argument(
              "Conflicting source colors: " + existing.label + " / " + p.label);
        existing.tolerance = std::max(existing.tolerance, p.tolerance);
        duplicate          = true;
        break;
      }
    if (!duplicate) pairs.push_back(p);
  }
  PaletteFitReport result;
  for (const ColorPair &p : pairs) {
    if (distance2(p.source, p.target) < 1e-12f)
      ++result.preservedColors;
    else
      ++result.changedColors;
  }
  // Accumulate a compact, smooth displacement field. A zero tolerance starts
  // from identity; the anchor projection below supplies the minimum grid-sized
  // neighborhood required to represent that requested color change.
  const size_t cells = static_cast<size_t>(size) * size * size;
  std::vector<float> weights(cells, 0.0f);
  std::vector<float> delta(cells * 3, 0.0f);
  for (const ColorPair &p : pairs) {
    checkCancelled(cancelled);
    if (p.tolerance <= 0) continue;
    int low[3], high[3];
    for (int c = 0; c < 3; ++c) {
      low[c]  = std::max(0, static_cast<int>(std::floor(
                                (p.source[c] - p.tolerance) * (size - 1))));
      high[c] = std::min(
          size - 1, static_cast<int>(
                        std::ceil((p.source[c] + p.tolerance) * (size - 1))));
    }
    for (int r = low[0]; r <= high[0]; ++r) {
      checkCancelled(cancelled);
      for (int g = low[1]; g <= high[1]; ++g)
        for (int b = low[2]; b <= high[2]; ++b) {
          const std::array<float, 3> rgb = {
              r / float(size - 1), g / float(size - 1), b / float(size - 1)};
          const float d = std::sqrt(distance2(rgb, p.source)) / p.tolerance;
          if (d >= 1) continue;
          const float t      = 1 - d;
          const float weight = t * t * t * t * (1 + 4 * d);
          const size_t cell  = (static_cast<size_t>(r) * size + g) * size + b;
          weights[cell] += weight;
          for (int c = 0; c < 3; ++c)
            delta[cell * 3 + c] += weight * (p.target[c] - p.source[c]);
        }
    }
  }
  for (size_t cell = 0; cell < cells; ++cell)
    for (int c = 0; c < 3; ++c)
      lut.rgb[cell * 3 + c] =
          std::clamp(lut.rgb[cell * 3 + c] +
                         delta[cell * 3 + c] / std::max(1.0f, weights[cell]),
                     0.0f, 1.0f);

  std::vector<Stencil> stencils;
  for (const ColorPair &p : pairs) stencils.push_back(stencil(p.source, size));
  // Project onto trilinear anchor constraints, including preservation anchors.
  // Clamped targets remain in gamut. Close incompatible colors may be
  // impossible on a coarse lattice, in which case validation rejects rather
  // than averaging.
  constexpr float acceptableError = 0.25f / 255.0f;
  for (int pass = 0; pass < 512; ++pass) {
    checkCancelled(cancelled);
    for (size_t n = 0; n < pairs.size(); ++n) {
      for (int c = 0; c < 3; ++c)
        projectChannel(lut, stencils[n], c, pairs[n].target[c]);
    }
    result.maximumError = 0;
    for (size_t n = 0; n < pairs.size(); ++n) {
      const auto actual = sample(lut, stencils[n]);
      for (int c = 0; c < 3; ++c)
        result.maximumError = std::max(
            result.maximumError, std::fabs(actual[c] - pairs[n].target[c]));
    }
    if (result.maximumError <= acceptableError) break;
  }
  if (result.maximumError > acceptableError)
    throw std::runtime_error(
        "The LUT grid cannot resolve these color mappings. Increase the grid "
        "size or adjust conflicting nearby colors.");
  if (report) *report = result;
  return lut;
}
}  // namespace otlut
