#include "ino_common.h"
#include "perlinnoise.h"
#include "stdfx.h"
#include "tfxparam.h"
#include "trasterfx.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

double renderedValue(const TDoubleParamP &parameter, double frame,
                     const TRenderSettings &settings) {
  const double shrink =
      0.5 * (settings.m_shrinkX + settings.m_shrinkY);
  const double scale = std::sqrt(std::abs(settings.m_affine.det()));
  return shrink > 0.0 ? parameter->getValue(frame) * scale / shrink : 0.0;
}

int inputMargin(double amplitude) {
  return amplitude > 0.0 ? tceil(amplitude) + 1 : 0;
}

void sampleBilinear(const std::vector<float> &source, int width, double x,
                    double y, float *sample) {
  const int x0 = tfloor(x);
  const int y0 = tfloor(y);
  const int x1 = x0 + 1;
  const int y1 = y0 + 1;
  const float xWeight = static_cast<float>(x - x0);
  const float yWeight = static_cast<float>(y - y0);
  const float *topLeft = &source[(y0 * width + x0) * ino::channels()];
  const float *topRight = &source[(y0 * width + x1) * ino::channels()];
  const float *bottomLeft = &source[(y1 * width + x0) * ino::channels()];
  const float *bottomRight = &source[(y1 * width + x1) * ino::channels()];

  for (int channel = 0; channel < ino::channels(); ++channel) {
    const float top = topLeft[channel] * (1.0f - xWeight) +
                      topRight[channel] * xWeight;
    const float bottom = bottomLeft[channel] * (1.0f - xWeight) +
                         bottomRight[channel] * xWeight;
    sample[channel] = top * (1.0f - yWeight) + bottom * yWeight;
  }
}

void roughenEdges(const std::vector<float> &source, std::vector<float> &out,
                  int sourceWidth, int outputWidth, int outputHeight,
                  int margin, double amplitude, double noiseScale,
                  double evolution, const TPointD &tilePosition) {
  PerlinNoise noise;

  for (int y = 0; y < outputHeight; ++y) {
    for (int x = 0; x < outputWidth; ++x) {
      const int sourceX = x + margin;
      const int sourceY = y + margin;
      const float *center =
          &source[(sourceY * sourceWidth + sourceX) * ino::channels()];
      const double globalX = tilePosition.x + x;
      const double globalY = tilePosition.y + y;
      const double displacementX =
          amplitude * (noise.Marble(globalX, globalY, evolution, noiseScale,
                                    0.0, 1.0) -
                       0.5) *
          2.0;
      const double displacementY =
          amplitude *
          (noise.Marble(globalX, globalY, evolution + 31.0, noiseScale, 0.0,
                        1.0) -
           0.5) *
          2.0;
      float sampled[4];
      sampleBilinear(source, sourceWidth, sourceX + displacementX,
                     sourceY + displacementY, sampled);
      float *destination = &out[(y * outputWidth + x) * ino::channels()];
      const bool opaqueInterior = center[3] >= 0.999f && sampled[3] >= 0.999f;
      for (int channel = 0; channel < ino::channels(); ++channel)
        destination[channel] = opaqueInterior ? center[channel] : sampled[channel];
    }
  }
}

class RasterLock final {
  TRasterP m_raster;

public:
  explicit RasterLock(const TRasterP &raster) : m_raster(raster) {
    m_raster->lock();
  }

  ~RasterLock() { m_raster->unlock(); }
};

}  // namespace

class RoughenEdgesFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(RoughenEdgesFx)

  TRasterFxPort m_source;
  TDoubleParamP m_amplitude;
  TDoubleParamP m_scale;
  TDoubleParamP m_evolution;

public:
  RoughenEdgesFx()
      : m_amplitude(2.0), m_scale(30.0), m_evolution(0.0) {
    addInputPort("Source", m_source);
    bindParam(this, "amplitude", m_amplitude);
    bindParam(this, "scale", m_scale);
    bindParam(this, "evolution", m_evolution);

    m_amplitude->setMeasureName("fxLength");
    m_amplitude->setValueRange(0.0, 100.0);
    m_scale->setMeasureName("fxLength");
    m_scale->setValueRange(1.0, 1000.0);
    enableComputeInFloat(true);
  }

  bool doGetBBox(double frame, TRectD &bBox,
                 const TRenderSettings &settings) override {
    if (!m_source.isConnected()) {
      bBox = TRectD();
      return false;
    }

    const bool hasBoundingBox = m_source->doGetBBox(frame, bBox, settings);
    if (hasBoundingBox)
      bBox = bBox.enlarge(inputMargin(
          std::abs(renderedValue(m_amplitude, frame, settings))));
    return hasBoundingBox;
  }

  void transform(double frame, int port, const TRectD &rectOnOutput,
                 const TRenderSettings &settingsOnOutput,
                 TRectD &rectOnInput,
                 TRenderSettings &settingsOnInput) override {
    settingsOnInput = settingsOnOutput;
    const int margin = inputMargin(
        std::abs(renderedValue(m_amplitude, frame, settingsOnOutput)));
    rectOnInput = margin > 0 ? rectOnOutput.enlarge(margin) : rectOnOutput;
  }

  int getMemoryRequirement(const TRectD &rect, double frame,
                           const TRenderSettings &settings) override {
    const int margin =
        inputMargin(std::abs(renderedValue(m_amplitude, frame, settings)));
    return TRasterFx::memorySize(rect.enlarge(margin), sizeof(float) * 8);
  }

  bool canHandle(const TRenderSettings &settings, double frame) override {
    return m_amplitude->getValue(frame) == 0.0 ||
           isAlmostIsotropic(settings.m_affine);
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &settings) override {
    if (!m_source.isConnected()) return;

    const double amplitude =
        std::abs(renderedValue(m_amplitude, frame, settings));
    if (amplitude == 0.0) {
      m_source->compute(tile, frame, settings);
      return;
    }

    const int margin = inputMargin(amplitude);
    const TDimension outputSize = tile.getRaster()->getSize();
    const TDimension sourceSize(outputSize.lx + margin * 2,
                               outputSize.ly + margin * 2);
    TTile sourceTile;
    m_source->allocateAndCompute(sourceTile,
                                 tile.m_pos - TPointD(margin, margin),
                                 sourceSize, tile.getRaster(), frame, settings);

    std::vector<float> source(sourceSize.lx * sourceSize.ly * ino::channels());
    std::vector<float> destination(outputSize.lx * outputSize.ly *
                                   ino::channels());
    RasterLock sourceLock(sourceTile.getRaster());
    RasterLock destinationLock(tile.getRaster());
    ino::ras_to_float_arr(sourceTile.getRaster(), ino::channels(), source.data());
    roughenEdges(source, destination, sourceSize.lx, outputSize.lx,
                 outputSize.ly, margin, amplitude,
                 std::max(1.0, renderedValue(m_scale, frame, settings)),
                 m_evolution->getValue(frame), tile.m_pos);
    ino::float_arr_to_ras(reinterpret_cast<const unsigned char *>(
                              destination.data()),
                          ino::channels(), tile.getRaster(), 0);
  }
};

FX_PLUGIN_IDENTIFIER(RoughenEdgesFx, "roughenEdgesFx")
