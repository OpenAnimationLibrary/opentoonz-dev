#include "stdfx.h"
#include "tfxparam.h"

#include "thresholdmaskfx_utils.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

enum ChannelMode {
  ChannelAlpha = 0,
  ChannelLuminance,
  ChannelRed,
  ChannelGreen,
  ChannelBlue
};
enum OutputMode { OutputMaskedSource = 0, OutputMatte };

template <typename PIXEL>
float normalizedChannel(const PIXEL &pix, int channel, bool unpremultiply) {
  const float invMax = 1.0f / static_cast<float>(PIXEL::maxChannelValue);
  const float alpha  = static_cast<float>(pix.m) * invMax;

  if (channel == ChannelAlpha) return alpha;

  float r = static_cast<float>(pix.r) * invMax;
  float g = static_cast<float>(pix.g) * invMax;
  float b = static_cast<float>(pix.b) * invMax;

  if (unpremultiply) {
    if (alpha <= 1e-6f) return 0.0f;
    r /= alpha;
    g /= alpha;
    b /= alpha;
  }

  switch (channel) {
  case ChannelLuminance:
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
  case ChannelRed:
    return r;
  case ChannelGreen:
    return g;
  case ChannelBlue:
    return b;
  default:
    return alpha;
  }
}

template <typename PIXEL>
PIXEL makeMattePixel(float matte) {
  const float maxChannel = static_cast<float>(PIXEL::maxChannelValue);
  const auto value = static_cast<typename PIXEL::Channel>(
      std::lround(ThresholdMaskFxUtils::clamp01(matte) * maxChannel));
  return PIXEL(value, value, value, value);
}

template <typename PIXEL>
void applyMatteToPixel(PIXEL &pix, float matte) {
  matte = ThresholdMaskFxUtils::clamp01(matte);
  pix.r = static_cast<typename PIXEL::Channel>(std::lround(pix.r * matte));
  pix.g = static_cast<typename PIXEL::Channel>(std::lround(pix.g * matte));
  pix.b = static_cast<typename PIXEL::Channel>(std::lround(pix.b * matte));
  pix.m = static_cast<typename PIXEL::Channel>(std::lround(pix.m * matte));
}

template <>
TPixelF makeMattePixel<TPixelF>(float matte) {
  matte = ThresholdMaskFxUtils::clamp01(matte);
  return TPixelF(matte, matte, matte, matte);
}

template <>
void applyMatteToPixel<TPixelF>(TPixelF &pix, float matte) {
  matte = ThresholdMaskFxUtils::clamp01(matte);
  pix.r *= matte;
  pix.g *= matte;
  pix.b *= matte;
  pix.m *= matte;
}

template <typename PIXEL>
void processRaster(TRasterPT<PIXEL> source, TRasterPT<PIXEL> mask,
                   int cropOffset, float threshold, float softness, int channel,
                   bool invert, bool unpremultiply, int featherRadius,
                   int outputMode) {
  const int maskLx = mask->getLx();
  const int maskLy = mask->getLy();
  std::vector<float> matte(maskLx * maskLy, 0.0f);

  mask->lock();
  for (int y = 0; y < maskLy; ++y) {
    const PIXEL *maskPix = mask->pixels(y);
    for (int x = 0; x < maskLx; ++x) {
      const float value = normalizedChannel(maskPix[x], channel, unpremultiply);
      float result = ThresholdMaskFxUtils::smoothThreshold(value, threshold,
                                                           softness);
      matte[y * maskLx + x] =
          ThresholdMaskFxUtils::applyInvert(result, invert);
    }
  }
  mask->unlock();

  ThresholdMaskFxUtils::boxBlur(matte, maskLx, maskLy, featherRadius);

  source->lock();
  const int sourceLx = source->getLx();
  const int sourceLy = source->getLy();
  for (int y = 0; y < sourceLy; ++y) {
    PIXEL *srcPix = source->pixels(y);
    const int matteY = y + cropOffset;
    for (int x = 0; x < sourceLx; ++x) {
      const int matteX = x + cropOffset;
      const float value = matte[matteY * maskLx + matteX];
      if (outputMode == OutputMatte)
        srcPix[x] = makeMattePixel<PIXEL>(value);
      else
        applyMatteToPixel(srcPix[x], value);
    }
  }
  source->unlock();
}

}  // namespace

class ThresholdMaskFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(ThresholdMaskFx)

  TRasterFxPort m_source;
  TRasterFxPort m_mask;

  TDoubleParamP m_threshold;
  TDoubleParamP m_softness;
  TDoubleParamP m_edgeFeather;
  TIntEnumParamP m_channel;
  TBoolParamP m_invert;
  TBoolParamP m_unpremultiplyMask;
  TIntEnumParamP m_output;

public:
  ThresholdMaskFx()
      : m_threshold(0.5)
      , m_softness(0.0)
      , m_edgeFeather(0.0)
      , m_channel(new TIntEnumParam(ChannelAlpha, "Alpha"))
      , m_invert(false)
      , m_unpremultiplyMask(true)
      , m_output(new TIntEnumParam(OutputMaskedSource, "Masked Source")) {
    addInputPort("Source", m_source);
    addInputPort("Mask", m_mask);

    bindParam(this, "threshold", m_threshold);
    bindParam(this, "softness", m_softness);
    bindParam(this, "edgeFeather", m_edgeFeather);
    bindParam(this, "channel", m_channel);
    bindParam(this, "invert", m_invert);
    bindParam(this, "unpremultiplyMask", m_unpremultiplyMask);
    bindParam(this, "output", m_output);

    m_threshold->setValueRange(0.0, 1.0);
    m_softness->setValueRange(0.0, 1.0);
    m_edgeFeather->setValueRange(0.0, 20.0);
    m_edgeFeather->setMeasureName("fxLength");

    m_channel->addItem(ChannelLuminance, "Luminance");
    m_channel->addItem(ChannelRed, "Red");
    m_channel->addItem(ChannelGreen, "Green");
    m_channel->addItem(ChannelBlue, "Blue");

    m_output->addItem(OutputMatte, "Matte");

    enableComputeInFloat(true);
  }

  bool doGetBBox(double frame, TRectD &bBox,
                 const TRenderSettings &info) override {
    if (!m_source.isConnected()) {
      bBox = TRectD();
      return false;
    }
    return m_source->doGetBBox(frame, bBox, info);
  }

  bool canHandle(const TRenderSettings &info, double frame) override {
    if (m_edgeFeather->getValue(frame) == 0.0) return true;
    return isAlmostIsotropic(info.m_affine);
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &ri) override {
    if (!m_source.isConnected() || !m_mask.isConnected()) {
      tile.getRaster()->clear();
      return;
    }

    m_source->compute(tile, frame, ri);

    const double shrink = 0.5 * (ri.m_shrinkX + ri.m_shrinkY);
    const double feather =
        std::abs(m_edgeFeather->getValue(frame) *
                 std::sqrt(std::abs(ri.m_affine.det())) / shrink);
    const int featherRadius = tceil(feather);

    const TPointD maskPos =
        tile.m_pos - TPointD(featherRadius, featherRadius);
    const TDimension maskSize(tile.getRaster()->getLx() + featherRadius * 2,
                              tile.getRaster()->getLy() + featherRadius * 2);

    TTile maskTile;
    m_mask->allocateAndCompute(maskTile, maskPos, maskSize, tile.getRaster(),
                               frame, ri);

    const float threshold = static_cast<float>(m_threshold->getValue(frame));
    const float softness  = static_cast<float>(m_softness->getValue(frame));
    const int channel     = m_channel->getValue();
    const bool invert     = m_invert->getValue();
    const bool unpremultiply = m_unpremultiplyMask->getValue();
    const int outputMode     = m_output->getValue();

    if (TRaster32P source = tile.getRaster()) {
      TRaster32P mask = maskTile.getRaster();
      if (!mask) throw TException("Threshold Mask: incompatible mask raster");
      processRaster<TPixel32>(source, mask, featherRadius, threshold, softness,
                              channel, invert, unpremultiply, featherRadius,
                              outputMode);
    } else if (TRaster64P source = tile.getRaster()) {
      TRaster64P mask = maskTile.getRaster();
      if (!mask) throw TException("Threshold Mask: incompatible mask raster");
      processRaster<TPixel64>(source, mask, featherRadius, threshold, softness,
                              channel, invert, unpremultiply, featherRadius,
                              outputMode);
    } else if (TRasterFP source = tile.getRaster()) {
      TRasterFP mask = maskTile.getRaster();
      if (!mask) throw TException("Threshold Mask: incompatible mask raster");
      processRaster<TPixelF>(source, mask, featherRadius, threshold, softness,
                             channel, invert, unpremultiply, featherRadius,
                             outputMode);
    } else {
      throw TException("Threshold Mask: unsupported raster type");
    }
  }
};

FX_PLUGIN_IDENTIFIER(ThresholdMaskFx, "thresholdMaskFx")
