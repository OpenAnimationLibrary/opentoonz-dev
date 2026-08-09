#include "stdfx.h"
#include "tfxparam.h"
#include "trop.h"
#include "trasterfx.h"

#include <algorithm>

namespace {

void includeRect(TRectD &bounds, const TRectD &rect, bool &hasBounds) {
  if (!hasBounds) {
    bounds    = rect;
    hasBounds = true;
    return;
  }

  bounds.x0 = std::min(bounds.x0, rect.x0);
  bounds.y0 = std::min(bounds.y0, rect.y0);
  bounds.x1 = std::max(bounds.x1, rect.x1);
  bounds.y1 = std::max(bounds.y1, rect.y1);
}

double trailOpacity(int index, int count, double startOpacity,
                    double endOpacity) {
  if (count == 1) return startOpacity;
  const double progress = static_cast<double>(index - 1) / (count - 1);
  return startOpacity + (endOpacity - startOpacity) * progress;
}

}  // namespace

class AfterimageTrailFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(AfterimageTrailFx)

  TRasterFxPort m_source;
  TIntParamP m_count;
  TIntParamP m_rate;
  TDoubleParamP m_startOpacity;
  TDoubleParamP m_endOpacity;

public:
  AfterimageTrailFx()
      : m_count(5), m_rate(2), m_startOpacity(0.8), m_endOpacity(0.0) {
    addInputPort("Source", m_source);
    bindParam(this, "count", m_count);
    bindParam(this, "rate", m_rate);
    bindParam(this, "start_opacity", m_startOpacity);
    bindParam(this, "end_opacity", m_endOpacity);

    m_count->setValueRange(1, 100);
    m_rate->setValueRange(1, 100);
    m_startOpacity->setValueRange(0.0, 1.0);
    m_endOpacity->setValueRange(0.0, 1.0);
    enableComputeInFloat(true);
  }

  bool doGetBBox(double frame, TRectD &bBox,
                 const TRenderSettings &settings) override {
    if (!m_source.isConnected()) {
      bBox = TRectD();
      return false;
    }

    const int count = m_count->getValue();
    const int rate  = m_rate->getValue();
    bool hasBounds  = false;
    for (int index = 0; index <= count; ++index) {
      TRectD sourceBounds;
      if (m_source->doGetBBox(frame - index * rate, sourceBounds, settings))
        includeRect(bBox, sourceBounds, hasBounds);
    }
    if (!hasBounds) bBox = TRectD();
    return hasBounds;
  }

  void transform(double frame, int port, const TRectD &rectOnOutput,
                 const TRenderSettings &settingsOnOutput,
                 TRectD &rectOnInput,
                 TRenderSettings &settingsOnInput) override {
    settingsOnInput = settingsOnOutput;
    rectOnInput     = rectOnOutput;
  }

  int getMemoryRequirement(const TRectD &rect, double frame,
                           const TRenderSettings &settings) override {
    return TRasterFx::memorySize(rect, settings.m_bpp);
  }

  bool canHandle(const TRenderSettings &settings, double frame) override {
    return true;
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &settings) override {
    if (!m_source.isConnected()) return;

    const int count = m_count->getValue();
    const int rate  = m_rate->getValue();
    const double startOpacity = m_startOpacity->getValue(frame);
    const double endOpacity   = m_endOpacity->getValue(frame);
    const TDimension tileSize = tile.getRaster()->getSize();
    tile.getRaster()->clear();

    for (int index = count; index >= 1; --index) {
      const double opacity =
          trailOpacity(index, count, startOpacity, endOpacity);
      if (opacity <= 0.0) continue;

      TTile afterimage;
      m_source->allocateAndCompute(afterimage, tile.m_pos, tileSize,
                                   tile.getRaster(), frame - index * rate,
                                   settings);
      TRop::rgbmScale(afterimage.getRaster(), afterimage.getRaster(), opacity,
                      opacity, opacity, opacity);
      TRop::over(tile.getRaster(), afterimage.getRaster());
    }

    TTile current;
    m_source->allocateAndCompute(current, tile.m_pos, tileSize,
                                 tile.getRaster(), frame, settings);
    TRop::over(tile.getRaster(), current.getRaster());
  }
};

FX_PLUGIN_IDENTIFIER(AfterimageTrailFx, "afterimageTrailFx")
