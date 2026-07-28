#pragma once

#include <QImage>
#include <QSize>
#include <QString>

namespace ExperimentalSvg {

struct RasterizationRequest {
  QString sourcePath;
  QSize outputSize;
  int maximumDimension = 16384;
};

struct RasterizationResult {
  QImage image;
  QSize naturalSize;
  QString error;

  bool isValid() const { return !image.isNull() && error.isEmpty(); }
};

class RasterizationService final {
public:
  static QSize naturalSize(const QString &sourcePath, QString *error = nullptr);
  static RasterizationResult rasterize(const RasterizationRequest &request);
};

}  // namespace ExperimentalSvg
