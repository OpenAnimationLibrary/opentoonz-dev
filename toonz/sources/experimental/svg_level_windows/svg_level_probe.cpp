#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSize>
#include <QStringList>
#include <QSvgRenderer>

#include <algorithm>
#include <iostream>
#include <limits>

namespace {

constexpr int kMaximumDimension = 16384;
constexpr qint64 kMaximumPixels =
    static_cast<qint64>(kMaximumDimension) * kMaximumDimension;

void printUsage(const char *programName) {
  std::cerr << "Usage: " << programName
            << " input.svg output.png [width height]\n";
}

bool parseDimension(const QString &text, int &value) {
  bool ok = false;
  const int parsed = text.toInt(&ok);
  if (!ok || parsed <= 0 || parsed > kMaximumDimension) return false;
  value = parsed;
  return true;
}

QSize naturalSvgSize(const QSvgRenderer &renderer) {
  QSize size = renderer.defaultSize();
  if (size.isValid() && !size.isEmpty()) return size;

  const QRectF viewBox = renderer.viewBoxF();
  if (viewBox.isValid() && viewBox.width() > 0.0 && viewBox.height() > 0.0) {
    const int width = std::max(1, qRound(viewBox.width()));
    const int height = std::max(1, qRound(viewBox.height()));
    return QSize(width, height);
  }

  return QSize();
}

bool isSafeOutputSize(const QSize &size) {
  if (!size.isValid() || size.isEmpty()) return false;
  if (size.width() > kMaximumDimension || size.height() > kMaximumDimension)
    return false;

  const qint64 pixels = static_cast<qint64>(size.width()) * size.height();
  return pixels > 0 && pixels <= kMaximumPixels;
}

}  // namespace

int main(int argc, char *argv[]) {
  QCoreApplication application(argc, argv);
  const QStringList arguments = application.arguments();

  if (arguments.size() != 3 && arguments.size() != 5) {
    printUsage(argv[0]);
    return 2;
  }

  const QString inputPath = arguments.at(1);
  const QString outputPath = arguments.at(2);

  const QFileInfo inputInfo(inputPath);
  if (!inputInfo.exists() || !inputInfo.isFile()) {
    std::cerr << "SVG input does not exist: "
              << inputPath.toLocal8Bit().constData() << "\n";
    return 3;
  }

  QSvgRenderer renderer(inputPath);
  if (!renderer.isValid()) {
    std::cerr << "Qt could not parse the SVG document: "
              << inputPath.toLocal8Bit().constData() << "\n";
    return 4;
  }

  QSize outputSize = naturalSvgSize(renderer);

  if (arguments.size() == 5) {
    int width = 0;
    int height = 0;
    if (!parseDimension(arguments.at(3), width) ||
        !parseDimension(arguments.at(4), height)) {
      std::cerr << "Width and height must be between 1 and "
                << kMaximumDimension << ".\n";
      return 5;
    }
    outputSize = QSize(width, height);
  }

  if (!isSafeOutputSize(outputSize)) {
    std::cerr << "The SVG has no usable intrinsic size. Supply explicit width "
                 "and height values within the probe limits.\n";
    return 6;
  }

  QImage frame(outputSize, QImage::Format_ARGB32_Premultiplied);
  if (frame.isNull()) {
    std::cerr << "Unable to allocate the output frame.\n";
    return 7;
  }
  frame.fill(Qt::transparent);

  QPainter painter(&frame);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  renderer.render(&painter, QRectF(QPointF(0.0, 0.0), QSizeF(outputSize)));
  painter.end();

  if (!frame.save(outputPath, "PNG")) {
    std::cerr << "Unable to write PNG output: "
              << outputPath.toLocal8Bit().constData() << "\n";
    return 8;
  }

  std::cout << "Rasterized " << inputPath.toLocal8Bit().constData() << " to "
            << outputSize.width() << "x" << outputSize.height() << " at "
            << outputPath.toLocal8Bit().constData() << "\n";
  return 0;
}
