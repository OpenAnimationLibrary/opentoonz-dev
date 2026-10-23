

#include "toonzqt/rasterimagedata.h"
#include "toonzqt/strokesdata.h"
#include "toonzqt/gutil.h"
#include "tcolorutils.h"
#include "tpaletteutil.h"
#include "trasterimage.h"
#include "trop.h"
#include "tstream.h"
#include "toonz/stage.h"
#include "toonz/tcenterlinevectorizer.h"
#include "toonz/ttileset.h"
#include "toonz/toonzimageutils.h"
#include "toonz/toonzscene.h"
#include "toonz/sceneproperties.h"
#include "toonz/tcamera.h"

#include <QDataStream>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace {
const char *const DrawingClipboardFormat =
    "application/x-opentoonz-raster-selection-v1";
constexpr qint64 MaxDrawingClipboardBytes = 256 * 1024 * 1024;

bool validRasterSize(int width, int height) {
  return width > 0 && height > 0 && width <= 16384 && height <= 16384 &&
         qint64(width) * height * 4 <= MaxDrawingClipboardBytes;
}

bool readClipboardBytes(QDataStream &stream, QByteArray &bytes) {
  quint32 size = 0;
  stream >> size;
  if (stream.status() != QDataStream::Ok) return false;
  if (size == 0xffffffffU) {
    bytes.clear();
    return true;
  }
  if (size > MaxDrawingClipboardBytes ||
      size > stream.device()->bytesAvailable())
    return false;
  bytes.resize(int(size));
  return stream.readRawData(bytes.data(), int(size)) == int(size);
}

TPaletteP paletteFromClipboardBytes(const QByteArray &bytes) {
  if (bytes.isEmpty()) return TPaletteP();
  QTemporaryDir dir;
  if (!dir.isValid()) return TPaletteP();
  QString name = dir.filePath("selection.tpl");
  QFile file(name);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
    return TPaletteP();
  file.close();
  TPersist *persist = nullptr;
  try {
    TIStream stream(TFilePath(name.toStdWString()));
    stream >> persist;
  } catch (...) {
    delete persist;
    return TPaletteP();
  }
  TPalette *palette = dynamic_cast<TPalette *>(persist);
  if (!palette) delete persist;
  return TPaletteP(palette);
}

TStroke selectionRectangle(const TRectD &rect) {
  std::vector<TThickPoint> points{
      rect.getP00(), (rect.getP00() + rect.getP01()) * 0.5,
      rect.getP01(), (rect.getP01() + rect.getP11()) * 0.5,
      rect.getP11(), (rect.getP11() + rect.getP10()) * 0.5,
      rect.getP10(), (rect.getP10() + rect.getP00()) * 0.5,
      rect.getP00()};
  TStroke stroke(points);
  stroke.setSelfLoop(true);
  return stroke;
}

bool validSelectionBounds(const TRectD &bounds) {
  const double limit = std::numeric_limits<int>::max() / 4.0;
  return !bounds.isEmpty() && std::isfinite(bounds.x0) &&
         std::isfinite(bounds.y0) && std::isfinite(bounds.x1) &&
         std::isfinite(bounds.y1) && std::abs(bounds.x0) <= limit &&
         std::abs(bounds.y0) <= limit && std::abs(bounds.x1) <= limit &&
         std::abs(bounds.y1) <= limit;
}

TPixel32 opaqueColor(const TPixel32 &pixel) {
  if (!pixel.m) return TPixel32::Black;
  // The selection raster is premultiplied. Keep alpha separately as CM tone.
  return TPixel32(std::min(255, (int(pixel.r) * 255 + pixel.m / 2) / pixel.m),
                  std::min(255, (int(pixel.g) * 255 + pixel.m / 2) / pixel.m),
                  std::min(255, (int(pixel.b) * 255 + pixel.m / 2) / pixel.m));
}

TPointD vectorizationDpi(ToonzScene *scene, double dpiX, double dpiY) {
  TPointD fallback = scene->getCurrentCamera()->getDpi();
  return TPointD(dpiX > 0.0 && std::isfinite(dpiX) ? dpiX : fallback.x,
                 dpiY > 0.0 && std::isfinite(dpiY) ? dpiY : fallback.y);
}

TVectorImageP vectorize(const TImageP &source, const TRectD &rect,
                        const VectorizerConfiguration &config,
                        TAffine transform) {
  VectorizerCore vc;
  TVectorImageP vi =
      vc.vectorize(source.getPointer(), config, source->getPalette());
  if (!vi) return TVectorImageP();
  vi->setPalette(source->getPalette());

  double dpiX, dpiY;
  TToonzImageP ti(source);
  TRasterImageP ri(source);
  if (ti)
    ti->getDpi(dpiX, dpiY);
  else if (ri)
    ri->getDpi(dpiX, dpiY);
  else
    return vi;

  if (dpiX <= 0.0 || dpiY <= 0.0 || !std::isfinite(dpiX) ||
      !std::isfinite(dpiY))
    return TVectorImageP();

  TScale sc(dpiX / Stage::inch, dpiY / Stage::inch);
  TTranslation tr(rect.getP00());

  for (int i = 0; i < (int)vi->getStrokeCount(); i++) {
    TStroke *stroke = vi->getStroke(i);
    // Restore the cropped raster's source origin before transforming the
    // selection, then convert source pixels to vector world coordinates.
    stroke->transform(sc.inv() * transform * tr, true);
  }
  return vi;
}

}  // namespace

//===================================================================
// RasterImageData
//-------------------------------------------------------------------

RasterImageData::RasterImageData()
    : m_dpiX(0), m_dpiY(0), m_transformation(), m_dim() {}

//-------------------------------------------------------------------

RasterImageData::~RasterImageData() = default;

//-------------------------------------------------------------------

RasterImageData *RasterImageData::fromClipboard(const QMimeData *mime) {
  if (!mime || !mime->hasFormat(DrawingClipboardFormat)) return nullptr;
  try {
    QByteArray bytes = mime->data(DrawingClipboardFormat);
    if (bytes.isEmpty() || bytes.size() > MaxDrawingClipboardBytes)
      return nullptr;
    QDataStream stream(&bytes, QIODevice::ReadOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    quint32 version = 0;
    bool cm         = false;
    double dpiX, dpiY, x0, y0, x1, y1, a11, a12, a13, a21, a22, a23;
    stream >> version >> cm >> dpiX >> dpiY >> x0 >> y0 >> x1 >> y1 >> a11 >>
        a12 >> a13 >> a21 >> a22 >> a23;
    if (stream.status() != QDataStream::Ok || version != 1 ||
        !std::isfinite(dpiX) || !std::isfinite(dpiY) || dpiX < 0.0 ||
        dpiY < 0.0 || !std::isfinite(a11) || !std::isfinite(a12) ||
        !std::isfinite(a13) || !std::isfinite(a21) || !std::isfinite(a22) ||
        !std::isfinite(a23))
      return nullptr;
    TRectD bounds(x0, y0, x1, y1);
    TAffine affine(a11, a12, a13, a21, a22, a23);
    if (!validSelectionBounds(bounds) || !validSelectionBounds(affine * bounds))
      return nullptr;

    QByteArray paletteData;
    if (!readClipboardBytes(stream, paletteData)) return nullptr;
    TPaletteP palette =
        cm ? paletteFromClipboardBytes(paletteData) : TPaletteP(new TPalette());
    if (!palette || palette->getPageCount() == 0 ||
        palette->getStyleCount() == 0 ||
        (cm && palette->getStyleCount() > TPixelCM32::getMaxInk() + 1))
      return nullptr;

    TRasterP raster;
    if (cm) {
      int width = 0, height = 0;
      stream >> width >> height;
      QByteArray compressed;
      if (stream.status() != QDataStream::Ok ||
          !validRasterSize(width, height) ||
          !readClipboardBytes(stream, compressed) || compressed.size() < 4)
        return nullptr;
      const auto *header =
          reinterpret_cast<const unsigned char *>(compressed.constData());
      const quint32 expandedSize = (quint32(header[0]) << 24) |
                                   (quint32(header[1]) << 16) |
                                   (quint32(header[2]) << 8) | header[3];
      if (expandedSize != qint64(width) * height * 4) return nullptr;
      QByteArray pixels = qUncompress(compressed);
      if (pixels.size() != expandedSize || !stream.atEnd()) return nullptr;
      QDataStream pixelStream(&pixels, QIODevice::ReadOnly);
      TRasterCM32P cmRaster(width, height);
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          quint32 value = 0;
          pixelStream >> value;
          TPixelCM32 pixel(value);
          if (pixel.getInk() >= palette->getStyleCount() ||
              pixel.getPaint() >= palette->getStyleCount())
            return nullptr;
          cmRaster->pixels(y)[x] = pixel;
        }
      }
      if (pixelStream.status() != QDataStream::Ok) return nullptr;
      raster = cmRaster;
    } else {
      // Qt 5.15 serializes a QImage as a non-null marker followed by PNG.
      // Read the dimensions before allowing the PNG reader to allocate pixels.
      qint32 marker = 0;
      stream >> marker;
      if (stream.status() != QDataStream::Ok || marker != 1) return nullptr;
      QImageReader reader(stream.device(), "png");
      QSize size = reader.size();
      if (!validRasterSize(size.width(), size.height())) return nullptr;
      QImage image = reader.read();
      if (image.isNull() || image.size() != size) return nullptr;
      raster = rasterFromQImage(image.convertToFormat(QImage::Format_ARGB32));
    }
    if (!raster || stream.status() != QDataStream::Ok) return nullptr;
    std::vector<TStroke> strokes(1, selectionRectangle(bounds));
    const std::vector<TRectD> rects;
    std::unique_ptr<RasterImageData> result(
        cm ? static_cast<RasterImageData *>(new ToonzImageData())
           : static_cast<RasterImageData *>(new FullColorImageData()));
    result->setData(raster, palette, dpiX, dpiY, raster->getSize(), rects,
                    strokes, strokes, affine);
    return result.release();
  } catch (...) {
    return nullptr;
  }
}

//===================================================================
// ToonzImageData
//-------------------------------------------------------------------

ToonzImageData::ToonzImageData()
    : RasterImageData(), m_copiedRaster(0), m_palette(new TPalette()) {}

//-------------------------------------------------------------------

ToonzImageData::ToonzImageData(const ToonzImageData &src)
    : m_copiedRaster(src.m_copiedRaster)
    , m_palette(src.m_palette)
    , m_usedStyles(src.m_usedStyles) {
  m_dpiX            = src.m_dpiX;
  m_dpiY            = src.m_dpiY;
  m_rects           = src.m_rects;
  m_strokes         = src.m_strokes;
  m_transformation  = src.m_transformation;
  m_originalStrokes = src.m_originalStrokes;
  m_dim             = src.m_dim;
  setImageData(src.imageData());
  for (const QString &format : src.formats()) {
    if (format != "application/x-qt-image")
      QMimeData::setData(format, src.data(format));
  }
  assert(m_palette);
}

//-------------------------------------------------------------------

ToonzImageData::~ToonzImageData() = default;

//-------------------------------------------------------------------

void ToonzImageData::setData(const TRasterP &copiedRaster,
                             const TPaletteP &palette, double dpiX, double dpiY,
                             const TDimension &dim,
                             const std::vector<TRectD> &rects,
                             const std::vector<TStroke> &strokes,
                             const std::vector<TStroke> &originalStrokes,
                             const TAffine &transformation) {
  m_copiedRaster    = copiedRaster;
  m_palette         = palette;
  m_dpiX            = dpiX;
  m_dpiY            = dpiY;
  m_rects           = rects;
  m_strokes         = strokes;
  m_transformation  = transformation;
  m_originalStrokes = originalStrokes;
  m_dim             = dim;

  // Build a list of used styles
  TToonzImageP ti(m_copiedRaster, m_copiedRaster->getBounds());
  ToonzImageUtils::getUsedStyles(m_usedStyles, ti);
}

//-------------------------------------------------------------------

void ToonzImageData::getData(TRasterP &copiedRaster, double &dpiX, double &dpiY,
                             std::vector<TRectD> &rects,
                             std::vector<TStroke> &strokes,
                             std::vector<TStroke> &originalStrokes,
                             TAffine &transformation,
                             TPalette *targetPalette) const {
  if (!m_copiedRaster || (m_rects.empty() && m_strokes.empty())) return;

  copiedRaster = m_copiedRaster->clone();
  dpiX         = m_dpiX;
  dpiY         = m_dpiY;
  assert(m_palette);

  rects.assign(m_rects.begin(), m_rects.end());
  strokes.assign(m_strokes.begin(), m_strokes.end());
  originalStrokes.assign(m_originalStrokes.begin(), m_originalStrokes.end());

  transformation     = m_transformation;
  TRasterCM32P cmRas = copiedRaster;

  // Manage temporary palette to avoid leak on early return
  std::unique_ptr<TPalette> tempPalette;
  if (!targetPalette) {
    tempPalette.reset(new TPalette());
    targetPalette = tempPalette.get();
  }

  if (!cmRas) return;  // tempPalette cleans up automatically

  std::set<int> usedStyles(m_usedStyles);
  TToonzImageP ti(cmRas, cmRas->getBounds());
  if (usedStyles.empty()) ToonzImageUtils::getUsedStyles(usedStyles, ti);

  std::map<int, int> indexTable;
  mergePalette(targetPalette, indexTable, m_palette, usedStyles);
  ToonzImageUtils::scrambleStyles(ti, indexTable);
  ti->setPalette(m_palette.getPointer());

  if (tempPalette) tempPalette.release();  // ownership transferred to caller
}

//-------------------------------------------------------------------

StrokesData *ToonzImageData::toStrokesData(ToonzScene *scene) const {
  if (!scene || !m_copiedRaster || !m_palette) return nullptr;
  try {
    TRectD rect;
    if (!m_rects.empty())
      rect = m_rects[0];
    else if (!m_strokes.empty())
      rect = m_strokes[0].getBBox();

    for (const auto &r : m_rects) rect += r;
    for (const auto &s : m_strokes) rect += s.getBBox();

    if (rect.isEmpty()) return nullptr;
    TToonzImageP image(m_copiedRaster->clone(), m_copiedRaster->getBounds());
    TPaletteP palette = m_palette->clone();
    image->setPalette(palette.getPointer());
    TPointD dpi = vectorizationDpi(scene, m_dpiX, m_dpiY);
    image->setDpi(dpi.x, dpi.y);

    const VectorizerParameters *vParams =
        scene->getProperties()->getVectorizerParameters();
    if (!vParams) return nullptr;

    CenterlineConfiguration cConf = vParams->getCenterlineConfiguration(0.0);
    NewOutlineConfiguration oConf = vParams->getOutlineConfiguration(0.0);

    const VectorizerConfiguration &config =
        vParams->m_isOutline
            ? static_cast<const VectorizerConfiguration &>(oConf)
            : static_cast<const VectorizerConfiguration &>(cConf);

    TVectorImageP vi = vectorize(image, rect, config, m_transformation);
    if (!vi) return nullptr;

    std::unique_ptr<StrokesData> sd(new StrokesData());

    std::set<int> indexes;
    for (int i = 0; i < (int)vi->getStrokeCount(); i++) indexes.insert(i);

    sd->setImage(vi, indexes);
    return sd.release();
  } catch (...) {
    return nullptr;
  }
}

//-------------------------------------------------------------------

int ToonzImageData::getMemorySize() const {
  int size = 0;
  for (const auto &s : m_strokes)
    size += s.getControlPointCount() * sizeof(TThickPoint) + 100;
  for (const auto &s : m_originalStrokes)
    size += s.getControlPointCount() * sizeof(TThickPoint) + 100;
  return size + sizeof(*(m_copiedRaster.getPointer())) +
         sizeof(*(m_palette.getPointer())) + sizeof(*this);
}

//===================================================================
// FullColorImageData
//-------------------------------------------------------------------

FullColorImageData::FullColorImageData()
    : RasterImageData(), m_copiedRaster(0), m_palette(new TPalette()) {}

//-------------------------------------------------------------------

FullColorImageData::FullColorImageData(const FullColorImageData &src)
    : m_copiedRaster(src.m_copiedRaster), m_palette(src.m_palette) {
  m_dpiX            = src.m_dpiX;
  m_dpiY            = src.m_dpiY;
  m_rects           = src.m_rects;
  m_strokes         = src.m_strokes;
  m_transformation  = src.m_transformation;
  m_originalStrokes = src.m_originalStrokes;
  m_dim             = src.m_dim;
  setImageData(src.imageData());
  for (const QString &format : src.formats()) {
    if (format != "application/x-qt-image")
      QMimeData::setData(format, src.data(format));
  }
}

//-------------------------------------------------------------------

FullColorImageData::~FullColorImageData() = default;

//-------------------------------------------------------------------

void FullColorImageData::setData(const TRasterP &copiedRaster,
                                 const TPaletteP &palette, double dpiX,
                                 double dpiY, const TDimension &dim,
                                 const std::vector<TRectD> &rects,
                                 const std::vector<TStroke> &strokes,
                                 const std::vector<TStroke> &originalStrokes,
                                 const TAffine &transformation) {
  m_copiedRaster    = copiedRaster;
  m_palette         = palette;
  m_dpiX            = dpiX;
  m_dpiY            = dpiY;
  m_rects           = rects;
  m_strokes         = strokes;
  m_originalStrokes = originalStrokes;
  m_transformation  = transformation;
  m_dim             = dim;
}

//-------------------------------------------------------------------

void FullColorImageData::getData(TRasterP &copiedRaster, double &dpiX,
                                 double &dpiY, std::vector<TRectD> &rects,
                                 std::vector<TStroke> &strokes,
                                 std::vector<TStroke> &originalStrokes,
                                 TAffine &transformation,
                                 TPalette *targetPalette) const {
  if (!m_copiedRaster || (m_rects.empty() && m_strokes.empty())) return;

  copiedRaster = m_copiedRaster->clone();
  dpiX         = m_dpiX;
  dpiY         = m_dpiY;

  rects.assign(m_rects.begin(), m_rects.end());
  strokes.assign(m_strokes.begin(), m_strokes.end());
  originalStrokes.assign(m_originalStrokes.begin(), m_originalStrokes.end());

  transformation = m_transformation;

  TRasterP ras = copiedRaster;
  if (!ras) return;
  if (!m_palette) return;

  std::unique_ptr<TPalette> tempPalette;
  if (!targetPalette) {
    tempPalette.reset(new TPalette());
    targetPalette = tempPalette.get();
  }

  std::set<int> usedStyles;
  TRasterImageP ri(ras);

  for (int i = 0; i < m_palette->getPageCount(); i++) {
    TPalette::Page *page = m_palette->getPage(i);
    for (int j = 0; j < page->getStyleCount(); j++)
      usedStyles.insert(page->getStyleId(j));
  }
  std::map<int, int> indexTable;
  mergePalette(targetPalette, indexTable, m_palette, usedStyles);
  ri->setPalette(m_palette.getPointer());

  if (tempPalette) tempPalette.release();  // ownership transferred to caller
}

//-------------------------------------------------------------------

StrokesData *FullColorImageData::toStrokesData(ToonzScene *scene) const {
  if (!scene || !m_copiedRaster) return nullptr;
  try {
    TRectD rect;
    if (!m_rects.empty())
      rect = m_rects[0];
    else if (!m_strokes.empty())
      rect = m_strokes[0].getBBox();

    for (const auto &r : m_rects) rect += r;
    for (const auto &s : m_strokes) rect += s.getBBox();

    if (rect.isEmpty()) return nullptr;
    TRasterImageP image(m_copiedRaster->clone());
    TPaletteP palette = m_palette ? m_palette->clone() : new TPalette();
    image->setPalette(palette.getPointer());
    TPointD dpi = vectorizationDpi(scene, m_dpiX, m_dpiY);
    image->setDpi(dpi.x, dpi.y);

    const VectorizerParameters *vParams =
        scene->getProperties()->getVectorizerParameters();
    if (!vParams) return nullptr;

    std::unique_ptr<VectorizerConfiguration> config(
        vParams->getCurrentConfiguration(0.0));
    if (!config) return nullptr;
    TVectorImageP vi = vectorize(image, rect, *config, m_transformation);
    if (!vi) return nullptr;

    std::unique_ptr<StrokesData> sd(new StrokesData());

    std::set<int> indexes;
    for (int i = 0; i < (int)vi->getStrokeCount(); i++) indexes.insert(i);

    sd->setImage(vi, indexes);
    return sd.release();
  } catch (...) {
    return nullptr;
  }
}

//-------------------------------------------------------------------

ToonzImageData *FullColorImageData::toToonzImageData() const {
  if (!m_copiedRaster ||
      !validRasterSize(m_copiedRaster->getLx(), m_copiedRaster->getLy()))
    return nullptr;
  try {
    TRaster32P rgba = m_copiedRaster;
    if (!rgba) {
      rgba = TRaster32P(m_copiedRaster->getSize());
      TRop::convert(rgba, m_copiedRaster);
    }

    constexpr int MaxColors  = 256;
    constexpr int MaxSamples = 65536;
    std::set<TPixel32> colors;
    qint64 visiblePixels = 0;
    for (int y = 0; y < rgba->getLy(); ++y) {
      const TPixel32 *row = rgba->pixels(y);
      for (int x = 0; x < rgba->getLx(); ++x) {
        if (!row[x].m) continue;
        ++visiblePixels;
        if (colors.size() <= MaxColors) colors.insert(opaqueColor(row[x]));
      }
    }

    if (colors.size() > MaxColors) {
      // Bound the clustering work and exclude transparent pixels. A single
      // sample row selects the quantizer's blended-image path, which also
      // handles narrow source images without dropping their border pixels.
      int count = int(std::min<qint64>(visiblePixels, MaxSamples));
      TRaster32P samples(count, 1);
      qint64 seen = 0;
      int sample  = 0;
      for (int y = 0; y < rgba->getLy() && sample < count; ++y) {
        const TPixel32 *row = rgba->pixels(y);
        for (int x = 0; x < rgba->getLx() && sample < count; ++x) {
          if (!row[x].m) continue;
          if (seen >= qint64(sample) * visiblePixels / count)
            samples->pixels(0)[sample++] = opaqueColor(row[x]);
          ++seen;
        }
      }
      colors.clear();
      TColorUtils::buildPalette(colors, samples, MaxColors);
      if (colors.empty()) return nullptr;
    }

    TPaletteP palette = new TPalette();
    std::map<TPixel32, int> styleIds;
    for (const TPixel32 &color : colors) {
      int styleId;
      if (styleIds.empty()) {
        styleId = 1;
        palette->setStyle(styleId, color);
      } else {
        styleId = palette->addStyle(color);
        palette->getPage(0)->addStyle(styleId);
      }
      styleIds[color] = styleId;
    }

    TRasterCM32P indexed(rgba->getSize());
    for (int y = 0; y < rgba->getLy(); ++y) {
      const TPixel32 *row = rgba->pixels(y);
      TPixelCM32 *out     = indexed->pixels(y);
      for (int x = 0; x < rgba->getLx(); ++x) {
        if (!row[x].m) {
          out[x] = TPixelCM32();
          continue;
        }
        TPixel32 color = opaqueColor(row[x]);
        auto found     = styleIds.find(color);
        int styleId;
        if (found != styleIds.end()) {
          styleId = found->second;
        } else {
          styleId = palette->getClosestStyle(color);
          if (styleId <= 0) return nullptr;
          if (styleIds.size() >= MaxSamples) styleIds.clear();
          styleIds[color] = styleId;
        }
        out[x] = TPixelCM32(styleId, 0, 255 - row[x].m);
      }
    }
    std::unique_ptr<ToonzImageData> result(new ToonzImageData());
    result->setData(indexed, palette, m_dpiX, m_dpiY, m_dim, m_rects, m_strokes,
                    m_originalStrokes, m_transformation);
    return result.release();
  } catch (...) {
    return nullptr;
  }
}

//-------------------------------------------------------------------

int FullColorImageData::getMemorySize() const {
  int size = 0;
  for (const auto &s : m_strokes)
    size += s.getControlPointCount() * sizeof(TThickPoint) + 100;
  for (const auto &s : m_originalStrokes)
    size += s.getControlPointCount() * sizeof(TThickPoint) + 100;
  return size + sizeof(*(m_copiedRaster.getPointer())) +
         sizeof(*(m_palette.getPointer())) + sizeof(*this);
}
