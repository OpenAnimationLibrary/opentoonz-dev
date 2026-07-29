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

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSvgRenderer>

#include <algorithm>

namespace SvgLevel {
namespace {

constexpr int kMaximumDimension = 16384;

QSize naturalSize(const QSvgRenderer &renderer) {
  QSize size = renderer.defaultSize();
  if (size.isValid() && !size.isEmpty()) return size;

  const QRectF viewBox = renderer.viewBoxF();
  if (!viewBox.isValid() || viewBox.width() <= 0.0 ||
      viewBox.height() <= 0.0)
    return QSize();

  return QSize(std::max(1, qRound(viewBox.width())),
               std::max(1, qRound(viewBox.height())));
}

bool isSafeSize(const QSize &size) {
  if (!size.isValid() || size.isEmpty()) return false;
  if (size.width() > kMaximumDimension || size.height() > kMaximumDimension)
    return false;

  const qint64 pixels = static_cast<qint64>(size.width()) * size.height();
  const qint64 maximumPixels =
      static_cast<qint64>(kMaximumDimension) * kMaximumDimension;
  return pixels > 0 && pixels <= maximumPixels;
}

bool prepareRenderer(const TFilePath &path, QSvgRenderer &renderer, QSize &size,
                     QString &error) {
  error.clear();

  const QString fileName = path.getQString();
  const QFileInfo fileInfo(fileName);
  if (!fileInfo.exists() || !fileInfo.isFile()) {
    error = QObject::tr("SVG source file does not exist.");
    return false;
  }

  if (!renderer.load(fileName) || !renderer.isValid()) {
    error = QObject::tr("The SVG document could not be parsed.");
    return false;
  }

  size = naturalSize(renderer);
  if (!isSafeSize(size)) {
    error = QObject::tr(
        "The SVG has no usable intrinsic size or exceeds the safety limit.");
    return false;
  }

  return true;
}

TRasterImageP rasterize(const TFilePath &path, QString &error) {
  QSvgRenderer renderer;
  QSize size;
  if (!prepareRenderer(path, renderer, size, error)) return TRasterImageP();

  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) {
    error = QObject::tr("Unable to allocate the SVG display frame.");
    return TRasterImageP();
  }
  image.fill(Qt::transparent);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  renderer.render(&painter, QRectF(QPointF(0.0, 0.0), QSizeF(size)));
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

// Rebuilds the generated display raster from the retained SVG source whenever
// the ImageManager cache is invalidated or evicted. Without this builder,
// TXshSimpleLevel::setFrame() would bind the legacy SVG ImageLoader, which
// converts the source to a TVectorImage on the next rebuild.
class SvgRasterImageBuilder final : public ImageBuilder {
  TFilePath m_path;
  TPointD m_dpi;
  TPalette *m_palette;
  QString m_lastError;

public:
  SvgRasterImageBuilder(const TFilePath &path, const TPointD &dpi,
                        TPalette *palette)
      : m_path(path), m_dpi(dpi), m_palette(palette) {}

  QString lastError() const { return m_lastError; }

protected:
  TImageP build(int imFlags, void *extData) override {
    Q_UNUSED(extData);

    TRasterImageP image = rasterize(m_path, m_lastError);
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
    if (!prepareRenderer(m_path, renderer, size, m_lastError)) return false;

    ImageBuilder::setImageInfo(info,
                               TDimension(size.width(), size.height()));
    info.m_dpix           = m_dpi.x;
    info.m_dpiy           = m_dpi.y;
    info.m_samplePerPixel = 4;
    info.m_bitsPerSample  = 8;
    return true;
  }
};

std::wstring uniqueLevelName(ToonzScene *scene,
                             const TFilePath &actualSvgPath,
                             const std::wstring &requestedName) {
  std::wstring baseName = requestedName.empty() ? actualSvgPath.getWideName()
                                                 : requestedName;
  NameModifier modifier(baseName);
  std::wstring candidate = modifier.getNext();
  while (scene->getLevelSet()->hasLevel(candidate))
    candidate = modifier.getNext();
  return candidate;
}

}  // namespace

OpenMode askOpenMode(QWidget *parent) {
  QApplication *application =
      qobject_cast<QApplication *>(QCoreApplication::instance());
  if (!application) return OpenMode::ConvertToToonzVector;
  if (!parent) parent = application->activeWindow();

  QMessageBox dialog(parent);
  dialog.setIcon(QMessageBox::Question);
  dialog.setWindowTitle(QObject::tr("Open SVG"));
  dialog.setText(QObject::tr("How should OpenToonz handle this SVG file?"));
  dialog.setInformativeText(QObject::tr(
      "Experimental SVG Levels preserve the SVG source and are read-only. "
      "Converting creates an editable Toonz Vector Level, but unsupported SVG "
      "features may be approximated or omitted."));

  QPushButton *openButton = dialog.addButton(
      QObject::tr("Open as SVG Level (Experimental)"), QMessageBox::AcceptRole);
  QPushButton *convertButton = dialog.addButton(
      QObject::tr("Convert to Toonz Vector Level"), QMessageBox::ActionRole);
  QPushButton *cancelButton =
      dialog.addButton(QObject::tr("Cancel"), QMessageBox::RejectRole);

  openButton->setToolTip(QObject::tr(
      "Preserve the SVG source and display a generated read-only raster frame."));
  convertButton->setToolTip(QObject::tr(
      "Use the existing SVG importer to create editable Toonz Vector artwork."));

  dialog.setDefaultButton(openButton);
  dialog.setEscapeButton(cancelButton);
  dialog.exec();

  if (dialog.clickedButton() == openButton)
    return OpenMode::OpenExperimentalSvgLevel;
  if (dialog.clickedButton() == convertButton)
    return OpenMode::ConvertToToonzVector;
  return OpenMode::Cancel;
}

TXshLevel *loadExperimentalLevel(ToonzScene *scene,
                                 const TFilePath &actualSvgPath,
                                 const std::wstring &requestedName) {
  if (!scene || actualSvgPath.getType() != "svg") return nullptr;

  const std::wstring name =
      uniqueLevelName(scene, actualSvgPath, requestedName);
  TXshSimpleLevel *level = new TXshSimpleLevel(name);
  level->setScene(scene);
  level->setType(SVG_XSHLEVEL);
  level->setPath(scene->codeFilePath(actualSvgPath), true);
  level->setPalette(FullColorPalette::instance()->getPalette(scene));

  const TPointD dpi = scene->getCurrentCamera()->getDpi();
  const TFrameId fid(1);
  const std::string imageId = level->getImageId(fid);

  SvgRasterImageBuilder *builder =
      new SvgRasterImageBuilder(actualSvgPath, dpi, level->getPalette());
  ImageManager::instance()->bind(imageId, builder);

  // Insert the frame without supplying pixels. The custom builder above then
  // generates and caches the first display raster through the normal frame API.
  level->setFrame(fid, TImageP());
  TRasterImageP image = level->getFrame(fid, false);
  if (!image) {
    QString error = builder->lastError();
    if (error.isEmpty())
      error = QObject::tr("The SVG display frame could not be generated.");
    level->eraseFrame(fid);
    delete level;
    QMessageBox::warning(QApplication::activeWindow(), QObject::tr("SVG Level"),
                         error);
    return nullptr;
  }

  level->setRenumberTable();
  level->setIsReadOnly(true);
  level->setDirtyFlag(false);

  LevelProperties *properties = level->getProperties();
  const TDimension resolution(image->getRaster()->getLx(),
                              image->getRaster()->getLy());
  properties->setImageRes(resolution);
  properties->setBpp(32);
  properties->setHasAlpha(true);
  properties->setDpiPolicy(LevelProperties::DP_CustomDpi);
  properties->setDpi(dpi);
  properties->setImageDpi(dpi);
  properties->setDoPremultiply(false);

  if (!scene->getLevelSet()->insertLevel(level)) {
    level->eraseFrame(fid);
    delete level;
    return nullptr;
  }

  return level;
}

}  // namespace SvgLevel

#endif  // _WIN32
