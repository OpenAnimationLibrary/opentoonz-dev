#include "stdfx.h"
#include "globalcontrollablefx.h"
#include "tfxparam.h"
#include "tnotanimatableparam.h"
#include "toonz/lut3d.h"

#include <QDateTime>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

namespace {
template <class PIXEL>
void applyLut(TRasterPT<PIXEL> raster, const Lut3D &lut) {
  const float maxValue = static_cast<float>(PIXEL::maxChannelValue);
  raster->lock();
  for (int y = 0; y < raster->getLy(); ++y) {
    PIXEL *pixel = raster->pixels(y);
    for (int x = 0; x < raster->getLx(); ++x, ++pixel) {
      if (pixel->m == 0) continue;
      const float alpha = static_cast<float>(pixel->m) / maxValue;
      float r           = static_cast<float>(pixel->r) / maxValue / alpha;
      float g           = static_cast<float>(pixel->g) / maxValue / alpha;
      float b           = static_cast<float>(pixel->b) / maxValue / alpha;
      lut.convert(r, g, b);
      pixel->r = static_cast<typename PIXEL::Channel>(
          tcrop(r * alpha * maxValue + 0.5f, 0.0f, maxValue));
      pixel->g = static_cast<typename PIXEL::Channel>(
          tcrop(g * alpha * maxValue + 0.5f, 0.0f, maxValue));
      pixel->b = static_cast<typename PIXEL::Channel>(
          tcrop(b * alpha * maxValue + 0.5f, 0.0f, maxValue));
    }
  }
  raster->unlock();
}

template <>
void applyLut<TPixelF>(TRasterFP raster, const Lut3D &lut) {
  raster->lock();
  for (int y = 0; y < raster->getLy(); ++y) {
    TPixelF *pixel = raster->pixels(y);
    for (int x = 0; x < raster->getLx(); ++x, ++pixel) {
      if (pixel->m <= 0.0f) continue;
      const float alpha = pixel->m;
      float r = pixel->r / alpha, g = pixel->g / alpha, b = pixel->b / alpha;
      lut.convert(r, g, b);
      pixel->r = r * alpha;
      pixel->g = g * alpha;
      pixel->b = b * alpha;
    }
  }
  raster->unlock();
}
}  // namespace

class Lut3DBakeFx final : public GlobalControllableFx {
  FX_PLUGIN_DECLARATION(Lut3DBakeFx)

  TRasterFxPort m_input;
  TStringParamP m_lutPath;
  mutable Lut3D m_lut;
  mutable QString m_loadedPath;
  mutable QDateTime m_loadedModified;
  mutable QMutex m_lutMutex;

  bool loadCurrentLut(QString &error) const {
    const QString path = QString::fromStdWString(m_lutPath->getValue());
    if (path.isEmpty()) return false;
    const QFileInfo info(path);
    QMutexLocker lock(&m_lutMutex);
    if (path == m_loadedPath && info.lastModified() == m_loadedModified &&
        m_lut.isValid())
      return true;
    Lut3D loaded;
    if (!loaded.load(path, &error)) return false;
    m_lut            = std::move(loaded);
    m_loadedPath     = path;
    m_loadedModified = info.lastModified();
    return true;
  }

public:
  Lut3DBakeFx() : m_lutPath(L"") {
    addInputPort("Source", m_input);
    bindParam(this, "lutFile", m_lutPath);
    enableComputeInFloat(true);
  }

  bool canHandle(const TRenderSettings &, double) override { return true; }

  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &info) override {
    if (!m_input.isConnected()) {
      bbox = TRectD();
      return false;
    }
    return m_input->doGetBBox(frame, bbox, info);
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &ri) override {
    if (!m_input.isConnected()) return;
    m_input->compute(tile, frame, ri);
    const QString path = QString::fromStdWString(m_lutPath->getValue());
    if (path.isEmpty()) return;
    QString error;
    if (!loadCurrentLut(error))
      throw TException((QString("3D LUT Bake: %1").arg(error)).toStdString());

    QMutexLocker lock(&m_lutMutex);
    if (TRasterFP raster = tile.getRaster())
      applyLut<TPixelF>(raster, m_lut);
    else if (TRaster64P raster = tile.getRaster())
      applyLut<TPixel64>(raster, m_lut);
    else if (TRaster32P raster = tile.getRaster())
      applyLut<TPixel32>(raster, m_lut);
    else
      throw TException("3D LUT Bake: unsupported pixel type");
  }
};

FX_PLUGIN_IDENTIFIER(Lut3DBakeFx, "lut3DBakeFx")
