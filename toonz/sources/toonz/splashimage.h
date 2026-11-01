#pragma once

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPixmap>
#include <QStringList>
#include <QSvgRenderer>
#include <QUrl>

// Startup-only Qt helper. No Preferences singleton or image I/O initialization
// is needed, so the chosen artwork is visible from the first splash frame.
namespace SplashImage {

inline QByteArray pngData(const QString &path, QSize *size = nullptr) {
  if (path.isEmpty()) return QByteArray();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return QByteArray();
  QImageReader reader(&file, "png");
  reader.setAutoDetectImageFormat(false);
  QImage image = reader.read();
  if (image.isNull()) return QByteArray();
  if (size) *size = image.size();
  if (!file.seek(0)) return QByteArray();
  return file.readAll();
}

inline QPixmap render(const QString &svgPath, const QStringList &pngPaths,
                      const QSize &maximumSize) {
  QFile file(svgPath);
  if (!file.open(QIODevice::ReadOnly)) return QPixmap();
  QDomDocument doc;
  if (!doc.setContent(file.readAll())) return QPixmap();

  QDomElement root = doc.documentElement();
  root.setAttribute("xmlns:xlink", "http://www.w3.org/1999/xlink");
  QDomNodeList images = root.elementsByTagName("image");
  QDomElement artwork;
  for (int i = 0; i < images.count(); ++i) {
    QDomElement image = images.at(i).toElement();
    QString href      = image.attribute("xlink:href", image.attribute("href"));
    if (image.attribute("id") == "artwork" ||
        image.attribute("id") == "splash-artwork")
      artwork = image;
    // QSvgRenderer loads from bytes below. Resolve existing relative images
    // against their SVG, never against the process's working directory.
    if (!href.isEmpty() && !href.startsWith("data:") &&
        QUrl(href).isRelative() && QDir::isRelativePath(href)) {
      href = QFileInfo(svgPath).dir().absoluteFilePath(href);
      image.setAttribute("xlink:href", href);
      image.setAttribute("href", href);
    }
  }

  // An explicit artwork slot avoids covering a custom SVG's text or guessing
  // which of its images should be replaced. Unmarked SVGs still render as-is.
  if (!artwork.isNull()) {
    for (const QString &pngPath : pngPaths) {
      QSize imageSize;
      QByteArray data = pngData(pngPath, &imageSize);
      if (data.isEmpty()) continue;
      QString href =
          "data:image/png;base64," + QString::fromLatin1(data.toBase64());
      artwork.setAttribute("xlink:href", href);
      artwork.setAttribute("href", href);
      // Qt 5's SVG image handler can stretch despite xMidYMid meet. Fit the
      // image geometry explicitly so swapping differently sized PNGs is safe.
      bool widthOk, heightOk;
      double width  = artwork.attribute("width").toDouble(&widthOk);
      double height = artwork.attribute("height").toDouble(&heightOk);
      QString aspect =
          artwork.attribute("preserveAspectRatio", "xMidYMid meet");
      if (widthOk && heightOk && width > 0 && height > 0 && aspect != "none" &&
          !aspect.contains("slice")) {
        QSizeF fitted = QSizeF(imageSize).scaled(QSizeF(width, height),
                                                 Qt::KeepAspectRatio);
        double alignX = aspect.contains("xMin")   ? 0
                        : aspect.contains("xMax") ? 1
                                                  : 0.5;
        double alignY = aspect.contains("YMin")   ? 0
                        : aspect.contains("YMax") ? 1
                                                  : 0.5;
        artwork.setAttribute("x", artwork.attribute("x").toDouble() +
                                      (width - fitted.width()) * alignX);
        artwork.setAttribute("y", artwork.attribute("y").toDouble() +
                                      (height - fitted.height()) * alignY);
        artwork.setAttribute("width", fitted.width());
        artwork.setAttribute("height", fitted.height());
      }
      break;
    }
    if (artwork.attribute("xlink:href", artwork.attribute("href")).isEmpty())
      artwork.parentNode().removeChild(artwork);
  }

  QSvgRenderer renderer(doc.toByteArray());
  if (!renderer.isValid() || renderer.defaultSize().isEmpty()) return QPixmap();
  QSize size = renderer.defaultSize().scaled(maximumSize, Qt::KeepAspectRatio);
  QPixmap pixmap(size);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  renderer.render(&painter);
  return pixmap;
}

inline QPixmap load(const QString &customPng, const QString &stuffDir,
                    const QString &localSvg,
                    const QString &compiledSvg = ":Resources/splash.svg",
                    const QSize &maximumSize   = QSize(610, 480)) {
  QStringList pngPaths;
  pngPaths << customPng;
  if (!stuffDir.isEmpty()) pngPaths << QDir(stuffDir).filePath("splash.png");

  QPixmap pixmap = render(localSvg, pngPaths, maximumSize);
  if (!pixmap.isNull()) return pixmap;

  // A compiled resource has no adjacent disk directory. Try the executable's
  // adjacent PNG, then an adjacent Qt resource, before its embedded artwork.
  pngPaths << QFileInfo(localSvg).dir().filePath("splash.png")
           << QFileInfo(compiledSvg).dir().filePath("splash.png");
  return render(compiledSvg, pngPaths, maximumSize);
}

}  // namespace SplashImage
