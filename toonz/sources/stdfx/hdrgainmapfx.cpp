#include "stdfx.h"
#include "hdrgainmapmath.h"
#include "tfxparam.h"
#include "trop.h"

namespace {

class RasterLock {
  TRasterP m_raster;

public:
  explicit RasterLock(const TRasterP &raster) : m_raster(raster) {
    m_raster->lock();
  }
  ~RasterLock() { m_raster->unlock(); }
  RasterLock(const RasterLock &)            = delete;
  RasterLock &operator=(const RasterLock &) = delete;
};

TRenderSettings inputSettings(const TRenderSettings &settings) {
  TRenderSettings result = settings;
  result.m_bpp           = 128;
  return result;
}

TRasterFP linearInput(TRasterFxPort &port, const TTile &output, double frame,
                      const TRenderSettings &settings) {
  // Request floating-point, linear output even when viewing a map at 8 bpc.
  // The template specifies the type/color space, not the requested dimensions.
  TRasterFP format(1, 1);
  format->setLinear(true);
  TTile input;
  port->allocateAndCompute(input, output.m_pos, output.getRaster()->getSize(),
                           format, frame, inputSettings(settings));
  return input.getRaster();
}

void outputRaster(const TRasterFP &result, TTile &tile,
                  const TRenderSettings &settings) {
  if (!tile.getRaster()->isLinear())
    TRop::tosRGB(result, settings.m_colorSpaceGamma);
  if (TRasterFP output = tile.getRaster())
    output->copy(result);
  else
    TRop::convert(tile.getRaster(), result);
}

}  // namespace

class HDRGainMapBaseFx : public TStandardRasterFx {
  TDoubleParamP m_minStops, m_maxStops, m_mapGamma, m_blackOffset;

protected:
  HDRGainMapBaseFx()
      : m_minStops(-8.0)
      , m_maxStops(8.0)
      , m_mapGamma(1.0)
      , m_blackOffset(1.0 / 64.0) {
    bindParam(this, "minStops", m_minStops);
    bindParam(this, "maxStops", m_maxStops);
    bindParam(this, "mapGamma", m_mapGamma);
    bindParam(this, "blackOffset", m_blackOffset);
    m_minStops->setValueRange(-32.0, 32.0);
    m_maxStops->setValueRange(-32.0, 32.0);
    m_mapGamma->setValueRange(0.125, 8.0);
    m_blackOffset->setValueRange(1e-6, 1.0);
    enableComputeInFloat(true);
  }

  HDRGainMap::Encoding encoding(double frame) const {
    HDRGainMap::Encoding result;
    result.minStops = m_minStops->getValue(frame);
    result.maxStops = m_maxStops->getValue(frame);
    result.gamma    = m_mapGamma->getValue(frame);
    result.offset   = m_blackOffset->getValue(frame);
    if (!result.isValid())
      throw TException(
          "HDR gain map: minimum stops must be below maximum "
          "stops; encoding settings must be finite and in range.");
    return result;
  }

public:
  bool canHandle(const TRenderSettings &, double) override { return true; }
  bool toBeComputedInLinearColorSpace(bool, bool tileIsLinear) const override {
    // Work in private linear float inputs; retain the caller's output space.
    // This also keeps Strength=0 and a missing map as true pass-throughs.
    return tileIsLinear;
  }
  int getMemoryRequirement(const TRectD &rect, double,
                           const TRenderSettings &) override {
    return 2 * TRasterFx::memorySize(rect, 128);
  }
};

class HDRGainMapFx final : public HDRGainMapBaseFx {
  FX_PLUGIN_DECLARATION(HDRGainMapFx)

  TRasterFxPort m_base, m_hdr;
  TIntEnumParamP m_mode;

public:
  HDRGainMapFx() : m_mode(new TIntEnumParam(HDRGainMap::RGB, "RGB")) {
    addInputPort("SDR", m_base);
    addInputPort("HDR", m_hdr);
    bindParam(this, "mode", m_mode);
    m_mode->addItem(HDRGainMap::Luminance, "Luminance");
    m_mode->addItem(HDRGainMap::MaxRGB, "Max RGB");
  }

  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &settings) override {
    bbox = TRectD();
    return m_base.isConnected() && m_hdr.isConnected() &&
           m_base->doGetBBox(frame, bbox, settings);
  }

  void doDryCompute(TRectD &rect, double frame,
                    const TRenderSettings &settings) override {
    if (!m_base.isConnected() || !m_hdr.isConnected()) return;
    TRectD baseRect(rect), hdrRect(rect);
    const auto info = inputSettings(settings);
    m_base->dryCompute(baseRect, frame, info);
    m_hdr->dryCompute(hdrRect, frame, info);
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &settings) override {
    if (!m_base.isConnected() || !m_hdr.isConnected()) {
      tile.getRaster()->clear();
      return;
    }
    const auto parameters = encoding(frame);
    const auto mode       = static_cast<HDRGainMap::Mode>(m_mode->getValue());
    TRasterFP base        = linearInput(m_base, tile, frame, settings);
    TRasterFP hdr         = linearInput(m_hdr, tile, frame, settings);
    {
      RasterLock baseLock(base), hdrLock(hdr);
      for (int y = 0; y < base->getLy(); ++y) {
        TPixelF *out      = base->pixels(y);
        const TPixelF *in = hdr->pixels(y);
        for (int x = 0; x < base->getLx(); ++x)
          out[x] = HDRGainMap::generate(out[x], in[x], parameters, mode);
      }
    }
    outputRaster(base, tile, settings);
  }
};

class ApplyHDRGainMapFx final : public HDRGainMapBaseFx {
  FX_PLUGIN_DECLARATION(ApplyHDRGainMapFx)

  TRasterFxPort m_base, m_map;
  TDoubleParamP m_strength;

public:
  ApplyHDRGainMapFx() : m_strength(100.0) {
    addInputPort("SDR", m_base);
    addInputPort("Gain Map", m_map);
    bindParam(this, "strength", m_strength);
    m_strength->setValueRange(0.0, 100.0);
  }

  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &settings) override {
    bbox = TRectD();
    return m_base.isConnected() && m_base->doGetBBox(frame, bbox, settings);
  }

  void doDryCompute(TRectD &rect, double frame,
                    const TRenderSettings &settings) override {
    if (!m_base.isConnected()) return;
    if (!m_map.isConnected() || m_strength->getValue(frame) <= 0.0) {
      m_base->dryCompute(rect, frame, settings);
      return;
    }
    TRectD baseRect(rect), mapRect(rect);
    const auto info = inputSettings(settings);
    m_base->dryCompute(baseRect, frame, info);
    m_map->dryCompute(mapRect, frame, info);
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &settings) override {
    if (!m_base.isConnected()) {
      tile.getRaster()->clear();
      return;
    }
    const double strength = m_strength->getValue(frame) / 100.0;
    if (!m_map.isConnected() || strength <= 0.0) {
      m_base->compute(tile, frame, settings);
      return;
    }
    const auto parameters = encoding(frame);
    TRasterFP base        = linearInput(m_base, tile, frame, settings);
    TRasterFP map         = linearInput(m_map, tile, frame, settings);
    {
      RasterLock baseLock(base), mapLock(map);
      for (int y = 0; y < base->getLy(); ++y) {
        TPixelF *out      = base->pixels(y);
        const TPixelF *in = map->pixels(y);
        for (int x = 0; x < base->getLx(); ++x)
          out[x] = HDRGainMap::apply(out[x], in[x], parameters, strength);
      }
    }
    outputRaster(base, tile, settings);
  }
};

FX_PLUGIN_IDENTIFIER(HDRGainMapFx, "hdrGainMapFx")
FX_PLUGIN_IDENTIFIER(ApplyHDRGainMapFx, "applyHDRGainMapFx")
