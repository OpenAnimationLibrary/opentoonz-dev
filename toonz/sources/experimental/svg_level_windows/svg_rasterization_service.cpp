#include "svg_rasterization_service.h"

#include <QFileInfo>
#include <QPainter>
#include <QRectF>
#include <QSvgRenderer>

#include <algorithm>

namespace ExperimentalSvg {
namespace {

QSize rendererNaturalSize(const QSvgRenderer &renderer) {
  QSize size = renderer.defaultSize();
  if (size.isValid() && !size.isEmpty()) return size;

  const QRectF viewBox = renderer.viewBoxF();
  if (viewBox.isValid() && viewBox.width() > 0.0 && viewBox.height() > 0.0) {
    return QSize(std::max(1, qRound(viewBox.width())),
                 std::max(1, qRound(viewBox.height())));
  }

  return QSize();
}

bool isSafeSize(const QSize &size, int maximumDimension) {
  if (!size.isValid() || size.isEmpty() || maximumDimension <= 0) return false;
  if (size.width() > maximumDimension || size.height() > maximumDimension)
    return false;

  const qint64 maximumPixels =
      static_cast<qint64>(maximumDimension) * maximumDimension;
  const qint64 pixels = static_cast<qint64>(size.width()) * size.height();
  return pixels > 0 && pixels <= maximumPixels;
}

}  // namespace

QSize RasterizationService::naturalSize(const QString &sourcePath,
                                        QString *error) {
  if (error) error->clear();

  const QFileInfo sourceInfo(sourcePath);
  if (!sourceInfo.exists() || !sourceInfo.isFile()) {
    if (error) *error = QStringLiteral("SVG source file does not exist.");
    return QSize();
  }

  QSvgRenderer renderer(sourcePath);
  if (!renderer.isValid()) {
    if (error) *error = QStringLiteral("The SVG document could not be parsed.");
    return QSize();
  }

  const QSize size = rendererNaturalSize(renderer);
  if ((!size.isValid() || size.isEmpty()) && error) {
    *error = QStringLiteral(
        "The SVG document has no usable intrinsic size or viewBox.");
  }
  return size;
}

RasterizationResult RasterizationService::rasterize(
    const RasterizationRequest &request) {
  RasterizationResult result;

  const QFileInfo sourceInfo(request.sourcePath);
  if (!sourceInfo.exists() || !sourceInfo.isFile()) {
    result.error = QStringLiteral("SVG source file does not exist.");
    return result;
  }

  QSvgRenderer renderer(request.sourcePath);
  if (!renderer.isValid()) {
    result.error = QStringLiteral("The SVG document could not be parsed.");
    return result;
  }

  result.naturalSize = rendererNaturalSize(renderer);
  const QSize outputSize = request.outputSize.isEmpty() ? result.naturalSize
                                                        : request.outputSize;
  if (!isSafeSize(outputSize, request.maximumDimension)) {
    result.error = QStringLiteral(
        "The requested SVG raster size is invalid or exceeds the safety limit.");
    return result;
  }

  QImage image(outputSize, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) {
    result.error = QStringLiteral("Unable to allocate the SVG raster frame.");
    return result;
  }
  image.fill(Qt::transparent);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  renderer.render(&painter, QRectF(QPointF(0.0, 0.0), QSizeF(outputSize)));
  painter.end();

  result.image = image;
  return result;
}

}  // namespace ExperimentalSvg
