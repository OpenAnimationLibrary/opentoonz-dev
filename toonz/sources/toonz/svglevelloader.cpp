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
#include "tsystem.h"

#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace SvgLevel {
namespace {

// This is a per-representation safety budget, not an SVG source-resolution
// limit. The retained SVG remains authoritative and can be rasterized again at
// a different resolution when the demand-aware cache is extended.
constexpr qint64 kRasterBudgetBytes = 256LL * 1024LL * 1024LL;
constexpr qint64 kBytesPerPixel     = 4;

// Foundation checkpoint: begin above the SVG's intrinsic pixel-sized viewport
// so ordinary Canvas viewing does not immediately magnify a low-resolution
// raster. Later this fixed baseline will be replaced by the agreed
// stage/view/Preview/render demand resolver and reusable resolution tiers.
constexpr double kInitialRasterDensity = 2.0;

struct SvgSourceFrame {
  TFrameId fid;
  TFilePath path;
};

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

QSize scaledRasterSize(const QSize &naturalSize) {
  if (!naturalSize.isValid() || naturalSize.isEmpty()) return QSize();

  const qint64 requestedWidth = std::max<qint64>(
      1, static_cast<qint64>(std::ceil(naturalSize.width() *
                                      kInitialRasterDensity)));
  const qint64 requestedHeight = std::max<qint64>(
      1, static_cast<qint64>(std::ceil(naturalSize.height() *
                                      kInitialRasterDensity)));

  const long double pixels = static_cast<long double>(requestedWidth) *
                             static_cast<long double>(requestedHeight);
  const qint64 maxPixels = kRasterBudgetBytes / kBytesPerPixel;
  if (pixels <= 0.0L || maxPixels <= 0) return QSize();

  double budgetScale = 1.0;
  if (pixels > static_cast<long double>(maxPixels))
    budgetScale = std::sqrt(static_cast<double>(maxPixels) /
                            static_cast<double>(pixels));

  const qint64 width = std::max<qint64>(
      1, static_cast<qint64>(std::floor(requestedWidth * budgetScale)));
  const qint64 height = std::max<qint64>(
      1, static_cast<qint64>(std::floor(requestedHeight * budgetScale)));

  if (width > std::numeric_limits<int>::max() ||
      height > std::numeric_limits<int>::max())
    return QSize();

  return QSize(static_cast<int>(width), static_cast<int>(height));
}

double rasterDensity(const QSize &naturalSize, const QSize &rasterSize) {
  if (!naturalSize.isValid() || naturalSize.isEmpty() ||
      !rasterSize.isValid() || rasterSize.isEmpty())
    return 1.0;

  const double sx = static_cast<double>(rasterSize.width()) /
                    static_cast<double>(naturalSize.width());
  const double sy = static_cast<double>(rasterSize.height()) /
                    static_cast<double>(naturalSize.height());
  return std::max(0.0001, std::min(sx, sy));
}

bool prepareRenderer(const TFilePath &path, QSvgRenderer &renderer,
                     QSize &naturalSize, QSize &rasterSize,
                     double &density) {
  const QString fileName = path.getQString();
  const QFileInfo fileInfo(fileName);
  if (!fileInfo.exists() || !fileInfo.isFile()) return false;
  if (!renderer.load(fileName) || !renderer.isValid()) return false;

  naturalSize = rendererNaturalSize(renderer);
  rasterSize  = scaledRasterSize(naturalSize);
  density     = rasterDensity(naturalSize, rasterSize);
  return naturalSize.isValid() && !naturalSize.isEmpty() &&
         rasterSize.isValid() && !rasterSize.isEmpty();
}

TRasterImageP rasterize(const TFilePath &path, const TPointD &baseDpi) {
  QSvgRenderer renderer;
  QSize naturalSize, rasterSize;
  double density = 1.0;
  if (!prepareRenderer(path, renderer, naturalSize, rasterSize, density))
    return TRasterImageP();

  QImage image(rasterSize, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) return TRasterImageP();
  image.fill(Qt::transparent);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  renderer.render(&painter,
                  QRectF(0.0, 0.0, static_cast<double>(rasterSize.width()),
                         static_cast<double>(rasterSize.height())));
  painter.end();

  TRaster32P raster(rasterSize.width(), rasterSize.height());
  raster->lock();
  for (int sourceY = 0; sourceY < rasterSize.height(); ++sourceY) {
    const QRgb *source =
        reinterpret_cast<const QRgb *>(image.constScanLine(sourceY));
    TPixel32 *destination =
        raster->pixels(rasterSize.height() - 1 - sourceY);
    for (int x = 0; x < rasterSize.width(); ++x) {
      const QRgb pixel = source[x];
      destination[x] =
          TPixel32(qRed(pixel), qGreen(pixel), qBlue(pixel), qAlpha(pixel));
    }
  }
  raster->unlock();

  TRasterImageP result(raster);
  result->setDpi(baseDpi.x * density, baseDpi.y * density);
  return result;
}

class SvgRasterImageBuilder final : public ImageBuilder {
  TFilePath m_path;
  TPointD m_baseDpi;
  TPalette *m_palette;

public:
  SvgRasterImageBuilder(const TFilePath &path, const TPointD &baseDpi,
                        TPalette *palette)
      : m_path(path), m_baseDpi(baseDpi), m_palette(palette) {}

protected:
  TImageP build(int imFlags, void *extData) override {
    Q_UNUSED(extData);

    TRasterImageP image = rasterize(m_path, m_baseDpi);
    if (!image) return TImageP();

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
    QSize naturalSize, rasterSize;
    double density = 1.0;
    if (!prepareRenderer(m_path, renderer, naturalSize, rasterSize, density))
      return false;

    ImageBuilder::setImageInfo(
        info, TDimension(rasterSize.width(), rasterSize.height()));
    info.m_dpix           = m_baseDpi.x * density;
    info.m_dpiy           = m_baseDpi.y * density;
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

TPointD baseLevelDpi(ToonzScene *scene) {
  if (!scene || !scene->getCurrentCamera()) return TPointD(120.0, 120.0);

  TPointD dpi = scene->getCurrentCamera()->getDpi();
  if (dpi.x <= 0.0 || dpi.y <= 0.0) return TPointD(120.0, 120.0);
  return dpi;
}

std::vector<SvgSourceFrame> collectSourceFrames(
    ToonzScene *scene, const TFilePath &actualSvgPath) {
  std::vector<SvgSourceFrame> frames;
  if (!scene) return frames;

  const TFilePath decodedPath = scene->decodeFilePath(actualSvgPath);

  if (!decodedPath.isLevelName()) {
    if (decodedPath.getType() == "svg" &&
        TFileStatus(decodedPath).doesExist())
      frames.push_back({TFrameId(1), decodedPath});
    return frames;
  }

  const TFilePathSet files =
      TSystem::readDirectory(decodedPath.getParentDir(), false, true);
  for (const TFilePath &file : files) {
    if (file.getType() != "svg") continue;
    if (file.getLevelName() != decodedPath.getLevelName()) continue;

    const TFrameId fid = file.getFrame();
    if (fid.isEmptyFrame()) continue;
    frames.push_back({fid, file});
  }

  std::sort(frames.begin(), frames.end(),
            [](const SvgSourceFrame &a, const SvgSourceFrame &b) {
              return a.fid < b.fid;
            });
  return frames;
}

bool bindDisplayFrame(TXshSimpleLevel *level, const SvgSourceFrame &source,
                      const TPointD &baseDpi, bool updateLevelProperties) {
  if (!level || !level->getScene()) return false;

  QSvgRenderer renderer;
  QSize naturalSize, rasterSize;
  double density = 1.0;
  if (!prepareRenderer(source.path, renderer, naturalSize, rasterSize, density))
    return false;

  ToonzScene *scene = level->getScene();
  if (!level->getPalette())
    level->setPalette(FullColorPalette::instance()->getPalette(scene));

  const std::string imageId = level->getImageId(source.fid);
  if (ImageManager::instance()->isBound(imageId))
    ImageManager::instance()->unbind(imageId);
  ImageManager::instance()->bind(
      imageId,
      new SvgRasterImageBuilder(source.path, baseDpi, level->getPalette()));

  if (!level->isFid(source.fid)) level->setFrame(source.fid, TImageP());

  if (updateLevelProperties) {
    // The raster is denser than the SVG's nominal scene-size baseline. Increase
    // representation DPI by the same amount so higher display resolution does
    // not make the level physically larger on stage.
    const TPointD representationDpi(baseDpi.x * density,
                                    baseDpi.y * density);
    LevelProperties *properties = level->getProperties();
    properties->setImageRes(
        TDimension(rasterSize.width(), rasterSize.height()));
    properties->setBpp(32);
    properties->setHasAlpha(true);
    properties->setDpiPolicy(LevelProperties::DP_CustomDpi);
    properties->setDpi(representationDpi);
    properties->setImageDpi(representationDpi);
    properties->setDoPremultiply(false);
  }

  return true;
}

}  // namespace

TXshLevel *loadRetainedLevel(ToonzScene *scene, const TFilePath &actualSvgPath,
                             const std::wstring &requestedName) {
  if (!scene || actualSvgPath.getType() != "svg") return nullptr;

  const std::vector<SvgSourceFrame> sources =
      collectSourceFrames(scene, actualSvgPath);
  if (sources.empty()) return nullptr;

  const std::wstring name =
      uniqueLevelName(scene, actualSvgPath, requestedName);

  TXshSimpleLevel *level = new TXshSimpleLevel(name);
  level->setScene(scene);

  // The display representation uses normal raster-level semantics. The path is
  // retained as the SVG file or SVG sequence pattern; each frame is bound to
  // its corresponding physical SVG source rather than being converted to PLI.
  level->setType(OVL_XSHLEVEL);
  level->setPath(scene->codeFilePath(actualSvgPath), true);
  level->setPalette(FullColorPalette::instance()->getPalette(scene));

  const TPointD baseDpi = baseLevelDpi(scene);
  bool firstFrame       = true;
  for (const SvgSourceFrame &source : sources) {
    if (!bindDisplayFrame(level, source, baseDpi, firstFrame)) {
      delete level;
      return nullptr;
    }
    firstFrame = false;
  }

  level->setRenumberTable();
  level->setIsReadOnly(true);
  level->setDirtyFlag(false);

  if (!scene->getLevelSet()->insertLevel(level)) {
    delete level;
    return nullptr;
  }

  return level;
}

}  // namespace SvgLevel

#endif
