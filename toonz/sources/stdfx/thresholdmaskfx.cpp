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
enum RegionMode { RegionAllPixels = 0, RegionPrompted };
enum ConnectivityMode { ConnectivityFour = 0, ConnectivityEight };
enum MaskInputMode {
  MaskInputMask = ThresholdMaskFxUtils::ConfidencePrimary,
  MaskInputSegmentation = ThresholdMaskFxUtils::ConfidenceSecondary,
  MaskInputIntersect = ThresholdMaskFxUtils::ConfidenceIntersect,
  MaskInputUnion = ThresholdMaskFxUtils::ConfidenceUnion
};

struct PromptSettings {
  bool enabled           = false;
  int seedX              = 0;
  int seedY              = 0;
  int searchRadius       = 0;
  bool eightConnected    = true;
  bool useBox            = false;
  ThresholdMaskFxUtils::IntRect box;
  bool useNegativePoint  = false;
  int negativeX          = 0;
  int negativeY          = 0;
  int negativeRadius     = 0;
};

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
                   TRasterPT<PIXEL> segmentation, int cropX, int cropY,
                   float threshold, float softness, int channel, bool invert,
                   bool unpremultiply, int featherRadius, int outputMode,
                   int maskInputMode, const PromptSettings &prompt) {
  TRasterPT<PIXEL> reference = mask ? mask : segmentation;
  if (!reference) return;

  const int maskLx = reference->getLx();
  const int maskLy = reference->getLy();
  std::vector<float> matte(maskLx * maskLy, 0.0f);

  if (mask) mask->lock();
  if (segmentation) segmentation->lock();
  for (int y = 0; y < maskLy; ++y) {
    const PIXEL *maskPix = mask ? mask->pixels(y) : nullptr;
    const PIXEL *segPix = segmentation ? segmentation->pixels(y) : nullptr;
    for (int x = 0; x < maskLx; ++x) {
      const float primary = maskPix
                                ? normalizedChannel(maskPix[x], channel,
                                                    unpremultiply)
                                : 0.0f;
      const float secondary = segPix
                                  ? normalizedChannel(segPix[x], channel,
                                                      unpremultiply)
                                  : 0.0f;
      const float confidence = ThresholdMaskFxUtils::combineConfidence(
          primary, secondary, maskInputMode);
      float result = ThresholdMaskFxUtils::smoothThreshold(
          confidence, threshold, softness);
      matte[y * maskLx + x] =
          ThresholdMaskFxUtils::applyInvert(result, invert);
    }
  }
  if (segmentation) segmentation->unlock();
  if (mask) mask->unlock();

  if (prompt.enabled) {
    if (prompt.useBox)
      ThresholdMaskFxUtils::clipOutsideRect(matte, maskLx, maskLy, prompt.box);
    if (prompt.useNegativePoint)
      ThresholdMaskFxUtils::eraseDisk(
          matte, maskLx, maskLy, prompt.negativeX, prompt.negativeY,
          prompt.negativeRadius);
    ThresholdMaskFxUtils::keepConnectedComponent(
        matte, maskLx, maskLy, prompt.seedX, prompt.seedY,
        prompt.searchRadius, prompt.eightConnected);
  }

  ThresholdMaskFxUtils::boxBlur(matte, maskLx, maskLy, featherRadius);

  source->lock();
  const int sourceLx = source->getLx();
  const int sourceLy = source->getLy();
  for (int y = 0; y < sourceLy; ++y) {
    PIXEL *srcPix  = source->pixels(y);
    const int maskY = y + cropY;
    for (int x = 0; x < sourceLx; ++x) {
      const int maskX = x + cropX;
      float value     = 0.0f;
      if (maskX >= 0 && maskX < maskLx && maskY >= 0 && maskY < maskLy)
        value = matte[maskY * maskLx + maskX];

      if (outputMode == OutputMatte)
        srcPix[x] = makeMattePixel<PIXEL>(value);
      else
        applyMatteToPixel(srcPix[x], value);
    }
  }
  source->unlock();
}

bool isFiniteRect(const TRectD &rect) {
  return rect != TConsts::infiniteRectD && std::isfinite(rect.x0) &&
         std::isfinite(rect.y0) && std::isfinite(rect.x1) &&
         std::isfinite(rect.y1);
}

TPointD toRenderPoint(const TPointD &point, const TRenderSettings &ri) {
  TPointD scaled(point.x / ri.m_shrinkX, point.y / ri.m_shrinkY);
  return ri.m_affine * scaled;
}

int scaledLength(double value, const TRenderSettings &ri) {
  const double shrink = 0.5 * (ri.m_shrinkX + ri.m_shrinkY);
  if (shrink <= 0.0) return 0;
  return tceil(std::abs(value) * std::sqrt(std::abs(ri.m_affine.det())) /
               shrink);
}

}  // namespace

class ThresholdMaskFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(ThresholdMaskFx)

  TRasterFxPort m_source;
  TRasterFxPort m_mask;
  TRasterFxPort m_segmentation;

  TDoubleParamP m_threshold;
  TDoubleParamP m_softness;
  TDoubleParamP m_edgeFeather;
  TIntEnumParamP m_channel;
  TBoolParamP m_invert;
  TBoolParamP m_unpremultiplyMask;
  TIntEnumParamP m_output;
  TIntEnumParamP m_maskInputMode;

  TIntEnumParamP m_regionMode;
  TPointParamP m_positivePoint;
  TDoubleParamP m_seedSearchRadius;
  TIntEnumParamP m_connectivity;
  TBoolParamP m_usePromptBox;
  TPointParamP m_boxMin;
  TPointParamP m_boxMax;
  TBoolParamP m_useNegativePoint;
  TPointParamP m_negativePoint;
  TDoubleParamP m_negativeRadius;

public:
  ThresholdMaskFx()
      : m_threshold(0.5)
      , m_softness(0.0)
      , m_edgeFeather(0.0)
      , m_channel(new TIntEnumParam(ChannelAlpha, "Alpha"))
      , m_invert(false)
      , m_unpremultiplyMask(true)
      , m_output(new TIntEnumParam(OutputMaskedSource, "Masked Source"))
      , m_maskInputMode(new TIntEnumParam(MaskInputMask, "Mask"))
      , m_regionMode(new TIntEnumParam(RegionAllPixels, "All Pixels"))
      , m_positivePoint(TPointD(0.0, 0.0))
      , m_seedSearchRadius(12.0)
      , m_connectivity(new TIntEnumParam(ConnectivityEight, "8-connected"))
      , m_usePromptBox(false)
      , m_boxMin(TPointD(-100.0, -100.0))
      , m_boxMax(TPointD(100.0, 100.0))
      , m_useNegativePoint(false)
      , m_negativePoint(TPointD(0.0, 0.0))
      , m_negativeRadius(3.0) {
    addInputPort("Source", m_source);
    addInputPort("Mask", m_mask);
    addInputPort("Segmentation", m_segmentation);

    bindParam(this, "threshold", m_threshold);
    bindParam(this, "softness", m_softness);
    bindParam(this, "edgeFeather", m_edgeFeather);
    bindParam(this, "channel", m_channel);
    bindParam(this, "invert", m_invert);
    bindParam(this, "unpremultiplyMask", m_unpremultiplyMask);
    bindParam(this, "output", m_output);
    bindParam(this, "maskInputMode", m_maskInputMode);

    bindParam(this, "regionMode", m_regionMode);
    bindParam(this, "positivePoint", m_positivePoint);
    bindParam(this, "seedSearchRadius", m_seedSearchRadius);
    bindParam(this, "connectivity", m_connectivity);
    bindParam(this, "usePromptBox", m_usePromptBox);
    bindParam(this, "boxMin", m_boxMin);
    bindParam(this, "boxMax", m_boxMax);
    bindParam(this, "useNegativePoint", m_useNegativePoint);
    bindParam(this, "negativePoint", m_negativePoint);
    bindParam(this, "negativeRadius", m_negativeRadius);

    m_threshold->setValueRange(0.0, 1.0);
    m_softness->setValueRange(0.0, 1.0);
    m_edgeFeather->setValueRange(0.0, 20.0);
    m_edgeFeather->setMeasureName("fxLength");

    m_channel->addItem(ChannelLuminance, "Luminance");
    m_channel->addItem(ChannelRed, "Red");
    m_channel->addItem(ChannelGreen, "Green");
    m_channel->addItem(ChannelBlue, "Blue");
    m_output->addItem(OutputMatte, "Matte");
    m_maskInputMode->addItem(MaskInputSegmentation, "Segmentation");
    m_maskInputMode->addItem(MaskInputIntersect, "Mask AND Segmentation");
    m_maskInputMode->addItem(MaskInputUnion, "Mask OR Segmentation");

    m_regionMode->addItem(RegionPrompted, "Prompted Region");
    m_connectivity->addItem(ConnectivityFour, "4-connected");
    m_seedSearchRadius->setValueRange(0.0, 100.0);
    m_seedSearchRadius->setMeasureName("fxLength");
    m_negativeRadius->setValueRange(0.0, 100.0);
    m_negativeRadius->setMeasureName("fxLength");

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
    const bool hasSpatialRadius = m_edgeFeather->getValue(frame) != 0.0 ||
                                  (m_regionMode->getValue() == RegionPrompted &&
                                   (m_seedSearchRadius->getValue(frame) != 0.0 ||
                                    (m_useNegativePoint->getValue() &&
                                     m_negativeRadius->getValue(frame) != 0.0)));
    return !hasSpatialRadius || isAlmostIsotropic(info.m_affine);
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &ri) override {
    if (!m_source.isConnected()) {
      tile.getRaster()->clear();
      return;
    }

    const int maskInputMode = m_maskInputMode->getValue();
    const bool needsMask = maskInputMode != MaskInputSegmentation;
    const bool needsSegmentation = maskInputMode != MaskInputMask;
    if ((needsMask && !m_mask.isConnected()) ||
        (needsSegmentation && !m_segmentation.isConnected())) {
      tile.getRaster()->clear();
      return;
    }

    m_source->compute(tile, frame, ri);

    const int featherRadius = scaledLength(m_edgeFeather->getValue(frame), ri);
    const bool prompted = m_regionMode->getValue() == RegionPrompted;

    TPointD maskPos;
    TDimension maskSize;
    int cropX = 0;
    int cropY = 0;

    if (prompted) {
      TRectD maskBBox;
      TRectD primaryBBox;
      TRectD secondaryBBox;
      bool hasPrimary = false;
      bool hasSecondary = false;
      if (needsMask)
        hasPrimary = m_mask->getBBox(frame, primaryBBox, ri) &&
                     !primaryBBox.isEmpty();
      if (needsSegmentation)
        hasSecondary = m_segmentation->getBBox(frame, secondaryBBox, ri) &&
                       !secondaryBBox.isEmpty();

      if ((needsMask && !hasPrimary) ||
          (needsSegmentation && !hasSecondary)) {
        tile.getRaster()->clear();
        return;
      }

      if (maskInputMode == MaskInputMask)
        maskBBox = primaryBBox;
      else if (maskInputMode == MaskInputSegmentation)
        maskBBox = secondaryBBox;
      else if (maskInputMode == MaskInputIntersect)
        maskBBox = primaryBBox * secondaryBBox;
      else
        maskBBox = primaryBBox + secondaryBBox;

      if (maskBBox.isEmpty()) {
        tile.getRaster()->clear();
        return;
      }
      if (!isFiniteRect(maskBBox)) {
        if (!ri.m_cameraBox.isEmpty() && isFiniteRect(ri.m_cameraBox))
          maskBBox = ri.m_cameraBox;
        else
          throw TException(
              "Threshold Mask: Prompted Region requires finite mask bounds");
      }

      const double x0 = std::floor(maskBBox.x0) - featherRadius;
      const double y0 = std::floor(maskBBox.y0) - featherRadius;
      const double x1 = std::ceil(maskBBox.x1) + featherRadius;
      const double y1 = std::ceil(maskBBox.y1) + featherRadius;
      const int lx = std::max(1, static_cast<int>(x1 - x0));
      const int ly = std::max(1, static_cast<int>(y1 - y0));
      const long long pixels = static_cast<long long>(lx) * ly;
      if (pixels > 64LL * 1024LL * 1024LL)
        throw TException("Threshold Mask: prompted mask exceeds 64M pixels");

      maskPos  = TPointD(x0, y0);
      maskSize = TDimension(lx, ly);
      cropX = static_cast<int>(std::lround(tile.m_pos.x - maskPos.x));
      cropY = static_cast<int>(std::lround(tile.m_pos.y - maskPos.y));
    } else {
      maskPos = tile.m_pos - TPointD(featherRadius, featherRadius);
      maskSize = TDimension(tile.getRaster()->getLx() + featherRadius * 2,
                            tile.getRaster()->getLy() + featherRadius * 2);
      cropX = featherRadius;
      cropY = featherRadius;
    }

    TTile maskTile;
    TTile segmentationTile;
    if (needsMask)
      m_mask->allocateAndCompute(maskTile, maskPos, maskSize, tile.getRaster(),
                                 frame, ri);
    if (needsSegmentation)
      m_segmentation->allocateAndCompute(segmentationTile, maskPos, maskSize,
                                         tile.getRaster(), frame, ri);

    PromptSettings prompt;
    if (prompted) {
      prompt.enabled        = true;
      prompt.eightConnected = m_connectivity->getValue() == ConnectivityEight;
      prompt.searchRadius =
          scaledLength(m_seedSearchRadius->getValue(frame), ri);

      const TPointD positive =
          toRenderPoint(m_positivePoint->getValue(frame), ri);
      prompt.seedX = static_cast<int>(std::lround(positive.x - maskPos.x));
      prompt.seedY = static_cast<int>(std::lround(positive.y - maskPos.y));

      prompt.useBox = m_usePromptBox->getValue();
      if (prompt.useBox) {
        const TPointD p0 = toRenderPoint(m_boxMin->getValue(frame), ri);
        const TPointD p1 = toRenderPoint(m_boxMax->getValue(frame), ri);
        prompt.box.x0 = static_cast<int>(
            std::floor(std::min(p0.x, p1.x) - maskPos.x));
        prompt.box.y0 = static_cast<int>(
            std::floor(std::min(p0.y, p1.y) - maskPos.y));
        prompt.box.x1 = static_cast<int>(
            std::ceil(std::max(p0.x, p1.x) - maskPos.x));
        prompt.box.y1 = static_cast<int>(
            std::ceil(std::max(p0.y, p1.y) - maskPos.y));
      }

      prompt.useNegativePoint = m_useNegativePoint->getValue();
      if (prompt.useNegativePoint) {
        const TPointD negative =
            toRenderPoint(m_negativePoint->getValue(frame), ri);
        prompt.negativeX =
            static_cast<int>(std::lround(negative.x - maskPos.x));
        prompt.negativeY =
            static_cast<int>(std::lround(negative.y - maskPos.y));
        prompt.negativeRadius =
            scaledLength(m_negativeRadius->getValue(frame), ri);
      }
    }

    const float threshold = static_cast<float>(m_threshold->getValue(frame));
    const float softness  = static_cast<float>(m_softness->getValue(frame));
    const int channel     = m_channel->getValue();
    const bool invert     = m_invert->getValue();
    const bool unpremultiply = m_unpremultiplyMask->getValue();
    const int outputMode     = m_output->getValue();

    if (TRaster32P source = tile.getRaster()) {
      TRaster32P mask;
      TRaster32P segmentation;
      if (needsMask) mask = maskTile.getRaster();
      if (needsSegmentation) segmentation = segmentationTile.getRaster();
      if ((needsMask && !mask) || (needsSegmentation && !segmentation))
        throw TException("Threshold Mask: incompatible mask raster");
      processRaster<TPixel32>(source, mask, segmentation, cropX, cropY,
                              threshold, softness, channel, invert,
                              unpremultiply, featherRadius, outputMode,
                              maskInputMode, prompt);
    } else if (TRaster64P source = tile.getRaster()) {
      TRaster64P mask;
      TRaster64P segmentation;
      if (needsMask) mask = maskTile.getRaster();
      if (needsSegmentation) segmentation = segmentationTile.getRaster();
      if ((needsMask && !mask) || (needsSegmentation && !segmentation))
        throw TException("Threshold Mask: incompatible mask raster");
      processRaster<TPixel64>(source, mask, segmentation, cropX, cropY,
                              threshold, softness, channel, invert,
                              unpremultiply, featherRadius, outputMode,
                              maskInputMode, prompt);
    } else if (TRasterFP source = tile.getRaster()) {
      TRasterFP mask;
      TRasterFP segmentation;
      if (needsMask) mask = maskTile.getRaster();
      if (needsSegmentation) segmentation = segmentationTile.getRaster();
      if ((needsMask && !mask) || (needsSegmentation && !segmentation))
        throw TException("Threshold Mask: incompatible mask raster");
      processRaster<TPixelF>(source, mask, segmentation, cropX, cropY, threshold,
                             softness, channel, invert, unpremultiply,
                             featherRadius, outputMode, maskInputMode, prompt);
    } else {
      throw TException("Threshold Mask: unsupported raster type");
    }
  }
};

FX_PLUGIN_IDENTIFIER(ThresholdMaskFx, "thresholdMaskFx")
