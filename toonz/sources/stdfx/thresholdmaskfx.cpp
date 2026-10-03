#include "stdfx.h"
#include "tfxparam.h"

#include "thresholdmaskfx_utils.h"

#include <algorithm>
#include <cmath>

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
float normalizedChannel(const PIXEL &pix, int channel) {
  const float invMax = 1.0f / static_cast<float>(PIXEL::maxChannelValue);

  switch (channel) {
  case ChannelAlpha:
    return static_cast<float>(pix.m) * invMax;
  case ChannelLuminance:
    return (0.2126f * static_cast<float>(pix.r) +
            0.7152f * static_cast<float>(pix.g) +
            0.0722f * static_cast<float>(pix.b)) *
           invMax;
  case ChannelRed:
    return static_cast<float>(pix.r) * invMax;
  case ChannelGreen:
    return static_cast<float>(pix.g) * invMax;
  case ChannelBlue:
    return static_cast<float>(pix.b) * invMax;
  default:
    return static_cast<float>(pix.m) * invMax;
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
                   float threshold, float softness, int channel, bool invert,
                   int outputMode) {
  source->lock();
  mask->lock();

  const int lx = source->getLx();
  const int ly = source->getLy();

  for (int y = 0; y < ly; ++y) {
    PIXEL *srcPix        = source->pixels(y);
    const PIXEL *maskPix = mask->pixels(y);
    for (int x = 0; x < lx; ++x) {
      const float value = normalizedChannel(maskPix[x], channel);
      float matte = ThresholdMaskFxUtils::smoothThreshold(value, threshold,
                                                          softness);
      matte = ThresholdMaskFxUtils::applyInvert(matte, invert);

      if (outputMode == OutputMatte)
        srcPix[x] = makeMattePixel<PIXEL>(matte);
      else
        applyMatteToPixel(srcPix[x], matte);
    }
  }

  mask->unlock();
  source->unlock();
}

}  // namespace

class ThresholdMaskFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(ThresholdMaskFx)

  TRasterFxPort m_source;
  TRasterFxPort m_mask;

  TDoubleParamP m_threshold;
  TDoubleParamP m_softness;
  TIntEnumParamP m_channel;
  TBoolParamP m_invert;
  TIntEnumParamP m_output;

public:
  ThresholdMaskFx()
      : m_threshold(0.5)
      , m_softness(0.0)
      , m_channel(new TIntEnumParam(ChannelAlpha, "Alpha"))
      , m_invert(false)
      , m_output(new TIntEnumParam(OutputMaskedSource, "Masked Source")) {
    addInputPort("Source", m_source);
    addInputPort("Mask", m_mask);

    bindParam(this, "threshold", m_threshold);
    bindParam(this, "softness", m_softness);
    bindParam(this, "channel", m_channel);
    bindParam(this, "invert", m_invert);
    bindParam(this, "output", m_output);

    m_threshold->setValueRange(0.0, 1.0);
    m_softness->setValueRange(0.0, 1.0);

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

  bool canHandle(const TRenderSettings &, double) override { return true; }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &ri) override {
    if (!m_source.isConnected() || !m_mask.isConnected()) {
      tile.getRaster()->clear();
      return;
    }

    m_source->compute(tile, frame, ri);

    TTile maskTile;
    m_mask->allocateAndCompute(maskTile, tile.m_pos, tile.getRaster()->getSize(),
                               tile.getRaster(), frame, ri);

    const float threshold = static_cast<float>(m_threshold->getValue(frame));
    const float softness  = static_cast<float>(m_softness->getValue(frame));
    const int channel     = m_channel->getValue();
    const bool invert     = m_invert->getValue();
    const int outputMode  = m_output->getValue();

    if (TRaster32P source = tile.getRaster()) {
      TRaster32P mask = maskTile.getRaster();
      if (!mask) throw TException("Threshold Mask: incompatible mask raster");
      processRaster<TPixel32>(source, mask, threshold, softness, channel,
                              invert, outputMode);
    } else if (TRaster64P source = tile.getRaster()) {
      TRaster64P mask = maskTile.getRaster();
      if (!mask) throw TException("Threshold Mask: incompatible mask raster");
      processRaster<TPixel64>(source, mask, threshold, softness, channel,
                              invert, outputMode);
    } else if (TRasterFP source = tile.getRaster()) {
      TRasterFP mask = maskTile.getRaster();
      if (!mask) throw TException("Threshold Mask: incompatible mask raster");
      processRaster<TPixelF>(source, mask, threshold, softness, channel, invert,
                             outputMode);
    } else {
      throw TException("Threshold Mask: unsupported raster type");
    }
  }
};

FX_PLUGIN_IDENTIFIER(ThresholdMaskFx, "thresholdMaskFx")
