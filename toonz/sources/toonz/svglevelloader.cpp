#include "svglevelloader.h"

#ifdef _WIN32

#include "toonz/fullcolorpalette.h"
#include "toonz/imagemanager.h"
#include "toonz/levelproperties.h"
#include "toonz/levelset.h"
#include "toonz/namebuilder.h"
#include "toonz/tcamera.h"
#include "toonz/textureutils.h"
#include "toonz/toonzscene.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"
#include "tpixel.h"
#include "trasterimage.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSvgRenderer>
#include <QXmlStreamReader>

#include <algorithm>

namespace SvgLevel {
namespace {

constexpr int kMaximumDimension = 16384;
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

bool isSafeSize(const QSize &size) {
  if (!size.isValid() || size.isEmpty()) return false;
  if (size.width() > kMaximumDimension || size.height() > kMaximumDimension)
    return false;

  const qint64 pixels = static_cast<qint64>(size.width()) * size.height();
  const qint64 maximumPixels =
      static_cast<qint64>(kMaximumDimension) * kMaximumDimension;
  return pixels > 0 && pixels <= maximumPixels;
}

bool isPhysicalFile(const TFilePath &path) {
  const QFileInfo fileInfo(path.getQString());
  return fileInfo.exists() && fileInfo.isFile();
}

TFilePath resolveSourcePath(ToonzScene *scene, const TFilePath &path) {
  TFilePath decodedPath = scene ? scene->decodeFilePath(path) : path;
  if (isPhysicalFile(decodedPath)) return decodedPath;

  const TFilePath noFramePath = decodedPath.withNoFrame();
  if (noFramePath != decodedPath && isPhysicalFile(noFramePath))
    return noFramePath;

  return decodedPath.getFrame().isEmptyFrame() ? noFramePath : decodedPath;
}

bool prepareRenderer(const TFilePath &path, QSvgRenderer &renderer, QSize &size,
                     QString &error) {
  error.clear();

  const QString fileName = path.getQString();
  const QFileInfo fileInfo(fileName);
  if (!fileInfo.exists() || !fileInfo.isFile()) {
    error = QObject::tr("SVG source file does not exist:\n%1")
                .arg(QDir::toNativeSeparators(fileName));
    return false;
  }

  if (!renderer.load(fileName) || !renderer.isValid()) {
    error = QObject::tr("The SVG document could not be parsed:\n%1")
                .arg(QDir::toNativeSeparators(fileName));
    return false;
  }

  size = rendererNaturalSize(renderer);
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

TPointD levelDpi(TXshSimpleLevel *level) {
  TPointD dpi = level->getProperties()->getDpi();
  if (dpi.x > 0.0 && dpi.y > 0.0) return dpi;

  ToonzScene *scene = level->getScene();
  return scene ? scene->getCurrentCamera()->getDpi() : TPointD(72.0, 72.0);
}

void updateLevelMetadata(TXshSimpleLevel *level, const TRasterImageP &image,
                         const TPointD &dpi) {
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

  level->setType(SVG_XSHLEVEL);
  level->setRenumberTable();
  level->setIsReadOnly(true);
  level->setDirtyFlag(false);
}

bool bindRenderedFrame(TXshSimpleLevel *level, const TFilePath &sourcePath,
                       const TRasterImageP &image, QString &error) {
  if (!level || !image) {
    error = QObject::tr("The SVG display frame could not be generated.");
    return false;
  }

  ToonzScene *scene = level->getScene();
  if (!scene) {
    error = QObject::tr("The SVG Level is not attached to a scene.");
    return false;
  }

  if (!level->getPalette())
    level->setPalette(FullColorPalette::instance()->getPalette(scene));

  const TPointD dpi = levelDpi(level);
  image->setDpi(dpi.x, dpi.y);
  image->setPalette(level->getPalette());

  const std::string imageId = level->getImageId(kSvgFrameId);
  ImageManager::instance()->bind(
      imageId,
      new SvgRasterImageBuilder(sourcePath, dpi, level->getPalette()));

  if (!level->isFid(kSvgFrameId))
    level->setFrame(kSvgFrameId, TImageP());

  if (!ImageManager::instance()->setImage(
          imageId, TImageP(image.getPointer()))) {
    error = QObject::tr("Unable to cache the SVG display frame.");
    return false;
  }

  updateLevelMetadata(level, image, dpi);
  texture_utils::invalidateTexture(level, kSvgFrameId);
  error.clear();
  return true;
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
      "Experimental SVG Levels preserve the SVG source and are read-only on "
      "the canvas. Their source can be edited in SVG Level Properties or "
      "Visual Studio Code. Converting creates an editable Toonz Vector Level, "
      "but unsupported SVG features may be approximated or omitted."));

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

bool validateSource(const QByteArray &source, QString *error, int *line,
                    int *column, QSize *naturalSize) {
  if (error) error->clear();
  if (line) *line = -1;
  if (column) *column = -1;
  if (naturalSize) *naturalSize = QSize();

  QXmlStreamReader xml(source);
  while (!xml.atEnd()) xml.readNext();
  if (xml.hasError()) {
    if (error) *error = xml.errorString();
    if (line) *line = static_cast<int>(xml.lineNumber());
    if (column) *column = static_cast<int>(xml.columnNumber());
    return false;
  }

  QSvgRenderer renderer(source);
  if (!renderer.isValid()) {
    if (error)
      *error = QObject::tr(
          "The XML is well-formed, but the SVG renderer cannot parse it.");
    return false;
  }

  const QSize size = rendererNaturalSize(renderer);
  if (!isSafeSize(size)) {
    if (error)
      *error = QObject::tr(
          "The SVG has no usable intrinsic size or exceeds the safety limit.");
    return false;
  }

  if (naturalSize) *naturalSize = size;
  return true;
}

TFilePath sourcePathForLevel(const TXshSimpleLevel *level) {
  if (!level) return TFilePath();
  return resolveSourcePath(level->getScene(), level->getPath());
}

bool restoreExperimentalLevel(TXshSimpleLevel *level, QString *error) {
  QString localError;
  if (!error) error = &localError;
  error->clear();

  if (!level || level->getType() != SVG_XSHLEVEL) {
    *error = QObject::tr("The level is not an SVG Level.");
    return false;
  }

  ToonzScene *scene = level->getScene();
  if (!scene) {
    *error = QObject::tr("The SVG Level is not attached to a scene.");
    return false;
  }

  const TFilePath sourcePath = sourcePathForLevel(level);
  if (!level->getPalette())
    level->setPalette(FullColorPalette::instance()->getPalette(scene));

  const TPointD dpi = levelDpi(level);
  const std::string imageId = level->getImageId(kSvgFrameId);
  ImageManager::instance()->bind(
      imageId,
      new SvgRasterImageBuilder(sourcePath, dpi, level->getPalette()));
  if (!level->isFid(kSvgFrameId))
    level->setFrame(kSvgFrameId, TImageP());

  TRasterImageP image = rasterize(sourcePath, *error);
  if (!image) {
    level->setIsReadOnly(true);
    level->setDirtyFlag(false);
    level->setRenumberTable();
    return false;
  }

  return bindRenderedFrame(level, sourcePath, image, *error);
}

bool reloadExperimentalLevel(TXshSimpleLevel *level, QString *error) {
  QString localError;
  if (!error) error = &localError;
  error->clear();

  if (!level || level->getType() != SVG_XSHLEVEL) {
    *error = QObject::tr("The level is not an SVG Level.");
    return false;
  }

  const TFilePath sourcePath = sourcePathForLevel(level);
  TRasterImageP image = rasterize(sourcePath, *error);
  if (!image) return false;

  return bindRenderedFrame(level, sourcePath, image, *error);
}

bool relinkExperimentalLevel(TXshSimpleLevel *level,
                             const TFilePath &newSourcePath, QString *error) {
  QString localError;
  if (!error) error = &localError;
  error->clear();

  if (!level || level->getType() != SVG_XSHLEVEL) {
    *error = QObject::tr("The level is not an SVG Level.");
    return false;
  }

  ToonzScene *scene = level->getScene();
  if (!scene) {
    *error = QObject::tr("The SVG Level is not attached to a scene.");
    return false;
  }

  const TFilePath resolvedPath = resolveSourcePath(scene, newSourcePath);
  TRasterImageP image = rasterize(resolvedPath, *error);
  if (!image) return false;

  const TFilePath oldPath = level->getPath();
  level->setPath(scene->codeFilePath(resolvedPath), true);
  if (!bindRenderedFrame(level, resolvedPath, image, *error)) {
    level->setPath(oldPath, true);
    return false;
  }

  return true;
}

TXshLevel *loadExperimentalLevel(ToonzScene *scene,
                                 const TFilePath &actualSvgPath,
                                 const std::wstring &requestedName) {
  if (!scene || actualSvgPath.getType() != "svg") return nullptr;

  const TFilePath sourcePath = resolveSourcePath(scene, actualSvgPath);
  const std::wstring name = uniqueLevelName(scene, sourcePath, requestedName);
  TXshSimpleLevel *level = new TXshSimpleLevel(name);
  level->setScene(scene);
  level->setType(SVG_XSHLEVEL);
  level->setPath(scene->codeFilePath(sourcePath), true);
  level->setPalette(FullColorPalette::instance()->getPalette(scene));

  QString error;
  if (!restoreExperimentalLevel(level, &error)) {
    level->eraseFrame(kSvgFrameId);
    delete level;
    QMessageBox::warning(QApplication::activeWindow(), QObject::tr("SVG Level"),
                         error.isEmpty()
                             ? QObject::tr(
                                   "The SVG display frame could not be generated.")
                             : error);
    return nullptr;
  }

  if (!scene->getLevelSet()->insertLevel(level)) {
    level->eraseFrame(kSvgFrameId);
    delete level;
    return nullptr;
  }

  return level;
}

}  // namespace SvgLevel

#endif
