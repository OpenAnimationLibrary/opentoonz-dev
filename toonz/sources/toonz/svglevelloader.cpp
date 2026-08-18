#include "svglevelloader.h"

#ifdef _WIN32

#include "toonz/fullcolorpalette.h"
#include "toonz/imagemanager.h"
#include "toonz/levelproperties.h"
#include "toonz/levelset.h"
#include "toonz/namebuilder.h"
#include "toonz/tcamera.h"
#include "toonz/toonzscene.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"
#include "tpixel.h"
#include "trasterimage.h"

#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>

namespace SvgLevel {
namespace {

// This is a per-representation safety budget, not an SVG source-resolution
// limit. The retained SVG remains authoritative and can be rasterized again at
// a different resolution when the demand-aware cache is extended.
constexpr qint64 kRasterBudgetBytes = 256LL * 1024LL * 1024LL;
constexpr qint64 kBytesPerPixel     = 4;
const TFrameId kSvgFrameId(1);

QSize rendererNaturalSize(const QSvgRenderer &renderer) {
  QSize size = renderer.defaultSize();
  if (size.isValid() && !size.isEmpty()) return size;

  const QRectF viewBox = renderer.viewBoxF();
  if (!viewBox.isValid() || viewBox.width() <= 0.0 ||
      viewBox.height() <= 0.0)
    return QSize();

  return QSize(std::max(1, qRound(viewBox.width())),
               std::max(1, qRound(viewBox.height())));
}

QSize clampToRasterBudget(const QSize &requested) {
  if (!requested.isValid() || requested.isEmpty()) return QSize();

  const qint64 pixels =
      static_cast<qint64>(requested.width()) * requested.height();
  const qint64 maxPixels = kRasterBudgetBytes / kBytesPerPixel;
  if (pixels <= 0 || maxPixels <= 0) return QSize();
  if (pixels <= maxPixels) return requested;

  const double scale =
      std::sqrt(static_cast<double>(maxPixels) / static_cast<double>(pixels));
  return QSize(std::max(1, static_cast<int>(std::floor(requested.width() * scale))),
               std::max(1, static_cast<int>(std::floor(requested.height() * scale))));
}

bool prepareRenderer(const TFilePath &path, QSvgRenderer &renderer,
                     QSize &rasterSize) {
  const QString fileName = path.getQString();
  const QFileInfo fileInfo(fileName);
  if (!fileInfo.exists() || !fileInfo.isFile()) return false;
  if (!renderer.load(fileName) || !renderer.isValid()) return false;

  rasterSize = clampToRasterBudget(rendererNaturalSize(renderer));
  return rasterSize.isValid() && !rasterSize.isEmpty();
}

TRasterImageP rasterize(const TFilePath &path) {
  QSvgRenderer renderer;
  QSize size;
  if (!prepareRenderer(path, renderer, size)) return TRasterImageP();

  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) return TRasterImageP();
  image.fill(Qt::transparent);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  renderer.render(&painter,
                  QRectF(0.0, 0.0, static_cast<double>(size.width()),
                         static_cast<double>(size.height())));
  painter.end();

  TRaster32P raster(size.width(), size.height());
  raster->lock();
  for (int sourceY = 0; sourceY < size.height(); ++sourceY) {
    const QRgb *source =
        reinterpret_cast<const QRgb *>(image.constScanLine(sourceY));
    TPixel32 *destination = raster->pixels(size.height() - 1 - sourceY);
    for (int x = 0; x < size.width(); ++x) {
      const QRgb pixel = source[x];
      destination[x] =
          TPixel32(qRed(pixel), qGreen(pixel), qBlue(pixel), qAlpha(pixel));
    }
  }
  raster->unlock();

  return TRasterImageP(raster);
}

class SvgRasterImageBuilder final : public ImageBuilder {
  TFilePath m_path;
  TPointD m_dpi;
  TPalette *m_palette;

public:
  SvgRasterImageBuilder(const TFilePath &path, const TPointD &dpi,
                        TPalette *palette)
      : m_path(path), m_dpi(dpi), m_palette(palette) {}

protected:
  TImageP build(int imFlags, void *extData) override {
    Q_UNUSED(extData);

    TRasterImageP image = rasterize(m_path);
    if (!image) return TImageP();

    image->setDpi(m_dpi.x, m_dpi.y);
    image->setPalette(m_palette);

    ImageBuilder::setImageInfo(m_info, image.getPointer());
    m_info.m_samplePerPixel = 4;
    m_info.m_bitsPerSample  = 8;
    m_imFlags               = imFlags & ImageManager::imageFlags;
    return image;
  }

  bool getInfo(TImageInfo &info, int imFlags, void *extData) override {
    Q_UNUSED(imFlags);
    Q_UNUSED(extData);

    QSvgRenderer renderer;
    QSize size;
    if (!prepareRenderer(m_path, renderer, size)) return false;

    ImageBuilder::setImageInfo(info, TDimension(size.width(), size.height()));
    info.m_dpix           = m_dpi.x;
    info.m_dpiy           = m_dpi.y;
    info.m_samplePerPixel = 4;
    info.m_bitsPerSample  = 8;
    return true;
  }
};

std::wstring uniqueLevelName(ToonzScene *scene, const TFilePath &actualSvgPath,
                             const std::wstring &requestedName) {
  std::wstring baseName = requestedName.empty() ? actualSvgPath.getWideName()
                                                 : requestedName;
  NameModifier modifier(baseName);
  std::wstring candidate = modifier.getNext();
  while (scene->getLevelSet()->hasLevel(candidate))
    candidate = modifier.getNext();
  return candidate;
}

TPointD levelDpi(TXshSimpleLevel *level) {
  TPointD dpi = level->getProperties()->getDpi();
  if (dpi.x > 0.0 && dpi.y > 0.0) return dpi;

  ToonzScene *scene = level->getScene();
  return scene ? scene->getCurrentCamera()->getDpi() : TPointD(120.0, 120.0);
}

bool bindDisplayFrame(TXshSimpleLevel *level, const TFilePath &sourcePath) {
  if (!level || !level->getScene()) return false;

  QSvgRenderer renderer;
  QSize size;
  if (!prepareRenderer(sourcePath, renderer, size)) return false;

  ToonzScene *scene = level->getScene();
  if (!level->getPalette())
    level->setPalette(FullColorPalette::instance()->getPalette(scene));

  const TPointD dpi = levelDpi(level);
  const std::string imageId = level->getImageId(kSvgFrameId);

  if (ImageManager::instance()->isBound(imageId))
    ImageManager::instance()->unbind(imageId);
  ImageManager::instance()->bind(
      imageId, new SvgRasterImageBuilder(sourcePath, dpi, level->getPalette()));

  if (!level->isFid(kSvgFrameId)) level->setFrame(kSvgFrameId, TImageP());

  LevelProperties *properties = level->getProperties();
  properties->setImageRes(TDimension(size.width(), size.height()));
  properties->setBpp(32);
  properties->setHasAlpha(true);
  properties->setDpiPolicy(LevelProperties::DP_CustomDpi);
  properties->setDpi(dpi);
  properties->setImageDpi(dpi);
  properties->setDoPremultiply(false);

  level->setRenumberTable();
  level->setIsReadOnly(true);
  level->setDirtyFlag(false);
  return true;
}

}  // namespace

TXshLevel *loadRetainedLevel(ToonzScene *scene, const TFilePath &actualSvgPath,
                             const std::wstring &requestedName) {
  if (!scene || actualSvgPath.getType() != "svg") return nullptr;

  const TFilePath sourcePath = scene->decodeFilePath(actualSvgPath);
  const std::wstring name = uniqueLevelName(scene, sourcePath, requestedName);

  TXshSimpleLevel *level = new TXshSimpleLevel(name);
  level->setScene(scene);

  // For this foundation checkpoint the display representation uses the normal
  // raster-level path. Retained-source identity will be serialized separately
  // rather than inventing a new raster semantics for SVG.
  level->setType(OVL_XSHLEVEL);
  level->setPath(scene->codeFilePath(sourcePath), true);
  level->setPalette(FullColorPalette::instance()->getPalette(scene));

  if (!bindDisplayFrame(level, sourcePath)) {
    delete level;
    return nullptr;
  }

  if (!scene->getLevelSet()->insertLevel(level)) {
    delete level;
    return nullptr;
  }

  return level;
}

}  // namespace SvgLevel

#endif
