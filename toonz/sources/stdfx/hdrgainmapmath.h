#pragma once

#ifndef HDR_GAIN_MAP_MATH_H
#define HDR_GAIN_MAP_MATH_H

#include "tpixel.h"

#include <array>
#include <cmath>
#include <limits>

namespace HDRGainMap {

enum Mode { RGB, Luminance, MaxRGB };

// These values are part of the map's encoding, not display exposure settings.
// Generation and application must use the same values, including after export.
struct Encoding {
  double minStops = -8.0;
  double maxStops = 8.0;
  double gamma    = 1.0;
  double offset   = 1.0 / 64.0;

  bool isValid() const {
    return std::isfinite(minStops) && std::isfinite(maxStops) &&
           minStops >= -32.0 && maxStops <= 32.0 && minStops < maxStops &&
           std::isfinite(gamma) && gamma >= 0.125 && gamma <= 8.0 &&
           std::isfinite(offset) && offset >= 1e-6 && offset <= 1.0;
  }
};

inline double alpha(const TPixelF &pixel) {
  return std::isfinite(pixel.m) ? std::max(0.0, std::min(1.0, double(pixel.m)))
                                : 0.0;
}

inline double nonnegative(double value) {
  return std::isfinite(value) ? std::max(0.0, value) : 0.0;
}

inline std::array<double, 3> straight(const TPixelF &pixel) {
  const double a = alpha(pixel);
  if (a <= 1e-8) return {{0.0, 0.0, 0.0}};
  return {{nonnegative(double(pixel.r) / a), nonnegative(double(pixel.g) / a),
           nonnegative(double(pixel.b) / a)}};
}

inline TPixelF premultiplied(const std::array<double, 3> &rgb, double a) {
  const double limit = (std::numeric_limits<float>::max)();
  return TPixelF(float(std::min(limit, nonnegative(rgb[0]) * a)),
                 float(std::min(limit, nonnegative(rgb[1]) * a)),
                 float(std::min(limit, nonnegative(rgb[2]) * a)), float(a));
}

inline double encodeStops(double stops, const Encoding &encoding) {
  const double normalized =
      std::max(0.0, std::min(1.0, (stops - encoding.minStops) /
                                      (encoding.maxStops - encoding.minStops)));
  return std::pow(normalized, encoding.gamma);
}

inline double decodeStops(double value, const Encoding &encoding) {
  if (!std::isfinite(value)) return 0.0;  // Invalid map channel: neutral gain.
  const double normalized =
      std::pow(std::max(0.0, std::min(1.0, value)), 1.0 / encoding.gamma);
  return encoding.minStops +
         normalized * (encoding.maxStops - encoding.minStops);
}

// Inputs and output are premultiplied, linear-light pixels. Normalized map
// channels are represented in that same linear working space for native I/O.
inline TPixelF generate(const TPixelF &base, const TPixelF &hdr,
                        const Encoding &encoding, Mode mode) {
  if (!encoding.isValid()) return TPixelF(0.f, 0.f, 0.f, 0.f);
  const double a = alpha(base);
  if (a <= 1e-8) return TPixelF(0.f, 0.f, 0.f, 0.f);

  const double neutral      = encodeStops(0.0, encoding);
  std::array<double, 3> map = {{neutral, neutral, neutral}};
  if (alpha(hdr) <= 1e-8) return premultiplied(map, a);

  const auto b = straight(base);
  const auto h = straight(hdr);
  auto gain    = [&](double sdr, double alternate) {
    return encodeStops(
           std::log2((alternate + encoding.offset) / (sdr + encoding.offset)),
           encoding);
  };
  if (mode == RGB) {
    for (int c = 0; c < 3; ++c) map[c] = gain(b[c], h[c]);
  } else {
    auto scalar = [mode](const std::array<double, 3> &rgb) {
      return mode == Luminance
                 ? 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]
                 : std::max(rgb[0], std::max(rgb[1], rgb[2]));
    };
    map.fill(gain(scalar(b), scalar(h)));
  }
  return premultiplied(map, a);
}

inline TPixelF apply(const TPixelF &base, const TPixelF &map,
                     const Encoding &encoding, double strength) {
  if (!encoding.isValid() || !std::isfinite(strength) || strength <= 0.0 ||
      alpha(map) <= 1e-8)
    return base;
  const double a = alpha(base);
  if (a <= 1e-8) return TPixelF(0.f, 0.f, 0.f, 0.f);

  const double weight                = std::min(1.0, strength);
  auto result                        = straight(base);
  const double mapAlpha              = alpha(map);
  const std::array<double, 3> values = {{double(map.r) / mapAlpha,
                                         double(map.g) / mapAlpha,
                                         double(map.b) / mapAlpha}};
  for (int c = 0; c < 3; ++c)
    result[c] = std::max(
        0.0, (result[c] + encoding.offset) *
                     std::exp2(weight * decodeStops(values[c], encoding)) -
                 encoding.offset);
  return premultiplied(result, a);
}

}  // namespace HDRGainMap

#endif
