#include "../splashimage.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char *message) {
  if (condition) return;
  // qFatal can report only to the debugger on Windows, hiding the assertion
  // from CTest. Always print the failure before returning a normal error code.
  std::fprintf(stderr, "FAIL: %s\n", message);
  std::fflush(stderr);
  std::exit(EXIT_FAILURE);
}

void write(const QString &path, const QByteArray &data) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly), "Cannot write fixture");
  require(file.write(data) == data.size(), "Cannot write complete fixture");
}

void png(const QString &path, const QColor &color,
         const QSize &size = QSize(4, 3)) {
  QImage image(size, QImage::Format_ARGB32);
  image.fill(color);
  require(image.save(path, "PNG"), "Cannot write PNG fixture");
}

QByteArray svg(const QByteArray &href = QByteArray()) {
  return "<svg xmlns=\"http://www.w3.org/2000/svg\" "
         "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
         "width=\"100\" height=\"100\" viewBox=\"0 0 100 100\">"
         "<rect width=\"100\" height=\"100\" fill=\"#00ff00\"/>"
         "<image id=\"artwork\" width=\"100\" height=\"80\" "
         "preserveAspectRatio=\"xMidYMid meet\" xlink:href=\"" +
         href +
         "\"/>"
         "<rect y=\"80\" width=\"100\" height=\"20\" fill=\"#ffff00\"/>"
         "<text x=\"5\" y=\"95\" font-size=\"10\">OT-DEV BUILD</text></svg>";
}

}  // namespace

int main(int argc, char **argv) {
  qInstallMessageHandler(
      [](QtMsgType, const QMessageLogContext &, const QString &message) {
        std::fprintf(stderr, "Qt: %s\n", qPrintable(message));
        std::fflush(stderr);
      });
  QGuiApplication app(argc, argv);
  const QString previousDirectory = QDir::currentPath();
  QTemporaryDir temp;
  require(temp.isValid(), "No temporary directory");
  QDir root(temp.path());
  root.mkdir("bin");
  root.mkdir("stuff");
  root.mkdir("resources");
  root.mkdir("unrelated");
  QString stuff       = root.filePath("stuff");
  QString localSvg    = root.filePath("bin/splash.svg");
  QString compiledSvg = root.filePath("resources/splash.svg");
  QString custom   = root.filePath(QString::fromUtf8("custom & \xc3\xa9.png"));
  QString stuffPng = root.filePath("stuff/splash.png");
  QString adjacentPng = root.filePath("bin/splash.png");
  QString resourcePng = root.filePath("resources/splash.png");
  write(localSvg, svg());
  write(compiledSvg, svg());
  require(QDir::setCurrent(root.filePath("unrelated")),
          "Cannot change directory");

  auto load = [&](const QString &preference = QString()) {
    QPixmap result = SplashImage::load(preference, stuff, localSvg, compiledSvg,
                                       QSize(100, 100));
    require(!result.isNull(), "Splash must render");
    require(result.size() == QSize(100, 100), "SVG dimensions changed");
    return result.toImage();
  };
  auto colorIs = [&](const QImage &image, const QColor &color,
                     const char *message) {
    require(image.pixelColor(50, 40) == color, message);
    // Sample the background outside the text: glyph widths vary by platform.
    require(image.pixelColor(0, 99) == QColor(Qt::yellow),
            "SVG footer changed");
  };

  QImage svgOnly = load();
  colorIs(svgOnly, Qt::green, "Local SVG must render without a PNG");
  png(stuffPng, Qt::blue);
  png(custom, Qt::red);
  colorIs(load(custom), Qt::red, "Preference must beat stuff PNG");
  colorIs(load(), Qt::blue, "Clearing preference must restore stuff PNG");
  colorIs(load(root.filePath("missing.png")), Qt::blue,
          "Missing preference fallback");
  write(custom, "corrupt PNG");
  colorIs(load(custom), Qt::blue, "Corrupt preference fallback");
  QImage jpeg(4, 3, QImage::Format_RGB32);
  jpeg.fill(Qt::red);
  require(jpeg.save(custom, "JPEG"), "Cannot write disguised JPEG fixture");
  colorIs(load(custom), Qt::blue, "Non-PNG must not be accepted");
  write(stuffPng, "corrupt PNG");
  png(adjacentPng, Qt::magenta);
  colorIs(load(), Qt::green, "Local SVG alone must precede executable PNG");

  // Replacing a file in-place must bypass QIcon's filename cache.
  png(stuffPng, Qt::red);
  colorIs(load(), Qt::red, "PNG replacement not loaded");
  png(stuffPng, Qt::blue);
  colorIs(load(), Qt::blue, "PNG replacement cached");
  require(QFile::remove(localSvg), "Cannot remove local SVG");
  colorIs(load(), Qt::blue, "Compiled SVG must use stuff PNG");
  png(custom, Qt::red);
  colorIs(load(custom), Qt::red, "Compiled SVG must use preference");
  require(QFile::remove(stuffPng), "Cannot remove stuff PNG");
  colorIs(load(), Qt::magenta, "Compiled SVG must use executable PNG");
  require(QFile::remove(adjacentPng), "Cannot remove executable PNG");
  png(resourcePng, Qt::cyan);
  colorIs(load(), Qt::cyan, "Compiled SVG must use adjacent resource PNG");
  QByteArray embedded =
      "data:image/png;base64," + SplashImage::pngData(resourcePng).toBase64();
  write(compiledSvg, svg(embedded));
  require(QFile::remove(resourcePng), "Cannot remove resource PNG");
  colorIs(load(), Qt::cyan, "Embedded artwork must survive as final fallback");
  write(localSvg, "invalid SVG");
  colorIs(load(), Qt::cyan, "Invalid local SVG must fall back to compiled SVG");

  // Relative references still resolve against the SVG when loaded from bytes.
  png(adjacentPng, Qt::magenta);
  write(localSvg, svg("splash.png"));
  colorIs(load(), Qt::magenta, "Relative SVG image used working directory");
  png(custom, QColor(255, 0, 0, 0));
  colorIs(load(custom), Qt::green, "PNG alpha must preserve SVG background");
  png(custom, Qt::red, QSize(4, 1));
  QImage wide = load(custom);
  colorIs(wide, Qt::red, "Wide PNG missing");
  require(wide.pixelColor(50, 5) == QColor(Qt::green),
          "PNG was stretched or cropped");
  require(wide.copy(0, 80, 100, 20) == svgOnly.copy(0, 80, 100, 20),
          "SVG build text changed with artwork");
  QPixmap bounded =
      SplashImage::load(custom, stuff, localSvg, compiledSvg, QSize(40, 30));
  require(bounded.size() == QSize(30, 30),
          "Splash bounds or aspect ratio ignored");
  require(QFile::remove(localSvg), "Cannot remove local SVG for resource test");
  png(custom, Qt::red);
  QPixmap resourceSplash = SplashImage::load(custom, stuff, localSvg);
  require(!resourceSplash.isNull(), "Real compiled resource did not render");
  require(resourceSplash.toImage().pixelColor(resourceSplash.width() / 2,
                                              resourceSplash.height() / 4) ==
              QColor(Qt::red),
          "Compiled resource artwork slot was not replaced");
  QPixmap aboutSplash = SplashImage::load(
      custom, stuff, localSvg, ":Resources/splash.svg", QSize(1220, 960));
  aboutSplash.setDevicePixelRatio(2);
  QSize logicalSize = aboutSplash.size() / aboutSplash.devicePixelRatio();
  // Fitting at physical pixel sizes may round one logical pixel differently.
  require(qAbs(logicalSize.width() - resourceSplash.width()) <= 1 &&
              qAbs(logicalSize.height() - resourceSplash.height()) <= 1,
          "About splash changed logical size on a high-DPI screen");
  require(QDir::setCurrent(previousDirectory),
          "Cannot restore working directory");
  std::puts("Splash image fallback and rendering checks passed.");
}
