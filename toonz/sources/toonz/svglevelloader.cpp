#include "svglevelloader.h"

#ifdef _WIN32

#include "toonz/fullcolorpalette.h"
#include "toonz/levelproperties.h"
#include "toonz/levelset.h"
#include "toonz/namebuilder.h"
#include "toonz/tcamera.h"
#include "toonz/toonzscene.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"
#include "tframeid.h"
#include "tpixel.h"
#include "trasterimage.h"

#include <QAbstractButton>
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

TRasterImageP rasterize(const TFilePath &path, QString &error) {
  error.clear();
  const QString fileName = path.getQString();
  const QFileInfo fileInfo(fileName);
  if (!fileInfo.exists() || !fileInfo.isFile()) {
    error = QObject::tr("SVG source file does not exist.");
    return TRasterImageP();
  }

  QSvgRenderer renderer(fileName);
  if (!renderer.isValid()) {
    error = QObject::tr("The SVG document could not be parsed.");
    return TRasterImageP();
  }

  const QSize size = naturalSize(renderer);
  if (!isSafeSize(size)) {
    error = QObject::tr(
        "The SVG has no usable intrinsic size or exceeds the safety limit.");
    return TRasterImageP();
  }

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

std::wstring uniqueLevelName(ToonzScene *scene,
                             const TFilePath &actualSvgPath,
                             const std::wstring &requestedName) {
  std::wstring baseName = requestedName.empty() ? actualSvgPath.getWideName()
                                                 : requestedName;
  NameModifier modifier(baseName);
  std::wstring candidate = modifier.getNext();
  while (scene->getLevelSet()->hasLevel(candidate)) candidate = modifier.getNext();
  return candidate;
}

}  // namespace

OpenMode askOpenMode(QWidget *parent) {
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

  QString error;
  TRasterImageP image = rasterize(actualSvgPath, error);
  if (!image) {
    QMessageBox::warning(nullptr, QObject::tr("SVG Level"), error);
    return nullptr;
  }

  const std::wstring name =
      uniqueLevelName(scene, actualSvgPath, requestedName);
  TXshSimpleLevel *level = new TXshSimpleLevel(name);
  level->setScene(scene);
  level->setType(SVG_XSHLEVEL);
  level->setPath(scene->codeFilePath(actualSvgPath), true);
  level->setPalette(FullColorPalette::instance()->getPalette(scene));
  level->setFrame(TFrameId(1), image);
  level->setIsReadOnly(true);
  level->setDirtyFlag(false);

  LevelProperties *properties = level->getProperties();
  const TDimension resolution(image->getRaster()->getLx(),
                              image->getRaster()->getLy());
  properties->setImageRes(resolution);
  properties->setBpp(32);
  properties->setDpiPolicy(LevelProperties::DP_CustomDpi);
  const TPointD dpi = scene->getCurrentCamera()->getDpi();
  properties->setDpi(dpi);
  properties->setImageDpi(dpi);

  if (!scene->getLevelSet()->insertLevel(level)) {
    delete level;
    return nullptr;
  }

  return level;
}

}  // namespace SvgLevel

#endif  // _WIN32
