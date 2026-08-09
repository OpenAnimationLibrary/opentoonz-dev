#include "ino_common.h"
#include "stdfx.h"
#include "tfxparam.h"
#include "trasterfx.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

double renderedRadius(const TDoubleParamP &radius, double frame,
                      const TRenderSettings &settings) {
  const double shrink =
      0.5 * (settings.m_shrinkX + settings.m_shrinkY);
  const double scale = std::sqrt(std::abs(settings.m_affine.det()));
  return shrink > 0.0 ? std::abs(radius->getValue(frame) * scale / shrink)
                      : 0.0;
}

int kernelRadius(double radius) {
  return radius > 0.0 ? std::max(1, tceil(radius)) : 0;
}

class RasterLock final {
  TRasterP m_raster;

public:
  explicit RasterLock(const TRasterP &raster) : m_raster(raster) {
    m_raster->lock();
  }

  ~RasterLock() { m_raster->unlock(); }
};

float colorDistanceSquared(const float *center, const float *neighbor) {
  const float centerAlpha   = std::max(center[3], 0.0f);
  const float neighborAlpha = std::max(neighbor[3], 0.0f);
  const float centerScale = centerAlpha > 0.0f ? 1.0f / centerAlpha : 0.0f;
  const float neighborScale =
      neighborAlpha > 0.0f ? 1.0f / neighborAlpha : 0.0f;

  const float red = center[0] * centerScale - neighbor[0] * neighborScale;
  const float green = center[1] * centerScale - neighbor[1] * neighborScale;
  const float blue = center[2] * centerScale - neighbor[2] * neighborScale;
  const float alpha = centerAlpha - neighborAlpha;
  return red * red + green * green + blue * blue + alpha * alpha;
}

void filterBilateral(const std::vector<float> &source, std::vector<float> &out,
                     int sourceWidth, int outputWidth, int outputHeight,
                     int radius, float colorThreshold) {
  const int diameter = radius * 2 + 1;
  const float spatialSigma = std::max(0.5f, radius * 0.5f);
  const float spatialScale = -0.5f / (spatialSigma * spatialSigma);
  const float rangeScale = -0.5f / (colorThreshold * colorThreshold);
  std::vector<float> spatialWeights(diameter * diameter);

  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      const int index = (y + radius) * diameter + x + radius;
      spatialWeights[index] = std::exp((x * x + y * y) * spatialScale);
    }
  }

  for (int y = 0; y < outputHeight; ++y) {
    for (int x = 0; x < outputWidth; ++x) {
      const int sourceX = x + radius;
      const int sourceY = y + radius;
      const float *center =
          &source[(sourceY * sourceWidth + sourceX) * ino::channels()];
      float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      float totalWeight = 0.0f;

      for (int neighborY = -radius; neighborY <= radius; ++neighborY) {
        for (int neighborX = -radius; neighborX <= radius; ++neighborX) {
          const float *neighbor =
              &source[((sourceY + neighborY) * sourceWidth + sourceX +
                       neighborX) *
                      ino::channels()];
          const int weightIndex =
              (neighborY + radius) * diameter + neighborX + radius;
          const float weight =
              spatialWeights[weightIndex] *
              std::exp(colorDistanceSquared(center, neighbor) * rangeScale);
          for (int channel = 0; channel < ino::channels(); ++channel)
            sum[channel] += neighbor[channel] * weight;
          totalWeight += weight;
        }
      }

      float *destination = &out[(y * outputWidth + x) * ino::channels()];
      for (int channel = 0; channel < ino::channels(); ++channel)
        destination[channel] = sum[channel] / totalWeight;
    }
  }
}

}  // namespace

class BilateralBlurFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(BilateralBlurFx)

  TRasterFxPort m_input;
  TDoubleParamP m_radius;
  TDoubleParamP m_colorThreshold;

public:
  BilateralBlurFx() : m_radius(5.0), m_colorThreshold(0.1) {
    addInputPort("Source", m_input);
    bindParam(this, "radius", m_radius);
    bindParam(this, "color_threshold", m_colorThreshold);

    m_radius->setMeasureName("fxLength");
    m_radius->setValueRange(0.0, 100.0);
    m_colorThreshold->setValueRange(0.001, 1.0);
    enableComputeInFloat(true);
  }

  bool doGetBBox(double frame, TRectD &bBox,
                 const TRenderSettings &settings) override {
    if (!m_input.isConnected()) {
      bBox = TRectD();
      return false;
    }

    const bool hasBoundingBox = m_input->doGetBBox(frame, bBox, settings);
    if (hasBoundingBox) {
      const int radius = kernelRadius(renderedRadius(m_radius, frame, settings));
      bBox = bBox.enlarge(radius);
    }
    return hasBoundingBox;
  }

  void transform(double frame, int port, const TRectD &rectOnOutput,
                 const TRenderSettings &settingsOnOutput,
                 TRectD &rectOnInput,
                 TRenderSettings &settingsOnInput) override {
    settingsOnInput = settingsOnOutput;
    rectOnInput = rectOnOutput.enlarge(
        kernelRadius(renderedRadius(m_radius, frame, settingsOnOutput)));
  }

  int getMemoryRequirement(const TRectD &rect, double frame,
                           const TRenderSettings &settings) override {
    const int radius = kernelRadius(renderedRadius(m_radius, frame, settings));
    return TRasterFx::memorySize(rect.enlarge(radius), sizeof(float) * 8);
  }

  bool canHandle(const TRenderSettings &settings, double frame) override {
    return m_radius->getValue(frame) == 0.0 ||
           isAlmostIsotropic(settings.m_affine);
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &settings) override {
    if (!m_input.isConnected()) return;

    const double radius = renderedRadius(m_radius, frame, settings);
    if (radius == 0.0) {
      m_input->compute(tile, frame, settings);
      return;
    }

    const int filterRadius = kernelRadius(radius);
    const TDimension outputSize = tile.getRaster()->getSize();
    const TDimension sourceSize(outputSize.lx + filterRadius * 2,
                                outputSize.ly + filterRadius * 2);
    TTile sourceTile;
    m_input->allocateAndCompute(sourceTile,
                                tile.m_pos - TPointD(filterRadius, filterRadius),
                                sourceSize, tile.getRaster(), frame, settings);

    std::vector<float> source(sourceSize.lx * sourceSize.ly * ino::channels());
    std::vector<float> destination(outputSize.lx * outputSize.ly *
                                   ino::channels());
    RasterLock sourceLock(sourceTile.getRaster());
    RasterLock destinationLock(tile.getRaster());
    ino::ras_to_float_arr(sourceTile.getRaster(), ino::channels(), source.data());
    filterBilateral(source, destination, sourceSize.lx, outputSize.lx,
                    outputSize.ly, filterRadius,
                    std::max(0.001f, static_cast<float>(
                                         m_colorThreshold->getValue(frame))));
    ino::float_arr_to_ras(reinterpret_cast<const unsigned char *>(
                              destination.data()),
                          ino::channels(), tile.getRaster(), 0);
  }
};

FX_PLUGIN_IDENTIFIER(BilateralBlurFx, "bilateralBlurFx")
