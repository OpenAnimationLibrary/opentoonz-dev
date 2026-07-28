#include "svg_open_mode_dialog.h"
#include "svg_rasterization_service.h"

#include <QApplication>
#include <QFileInfo>
#include <QStringList>

#include <iostream>

namespace {

constexpr int kMaximumDimension = 16384;

void printUsage(const char *programName) {
  std::cerr << "Usage: " << programName
            << " input.svg output.png [width height] [--ask]\n";
}

bool parseDimension(const QString &text, int &value) {
  bool ok = false;
  const int parsed = text.toInt(&ok);
  if (!ok || parsed <= 0 || parsed > kMaximumDimension) return false;
  value = parsed;
  return true;
}

}  // namespace

int main(int argc, char *argv[]) {
  QApplication application(argc, argv);
  const QStringList arguments = application.arguments();

  const bool askMode = arguments.contains(QStringLiteral("--ask"));
  QStringList positional = arguments;
  positional.removeAll(QStringLiteral("--ask"));

  if (positional.size() != 3 && positional.size() != 5) {
    printUsage(argv[0]);
    return 2;
  }

  if (askMode) {
    const ExperimentalSvg::OpenMode mode =
        ExperimentalSvg::OpenModeDialog::ask();
    if (mode == ExperimentalSvg::OpenMode::Cancel) return 0;
    if (mode == ExperimentalSvg::OpenMode::ConvertToToonzVector) {
      std::cout << "Selected existing SVG-to-PLI conversion path.\n";
      return 0;
    }
  }

  ExperimentalSvg::RasterizationRequest request;
  request.sourcePath = positional.at(1);
  request.maximumDimension = kMaximumDimension;

  if (positional.size() == 5) {
    int width = 0;
    int height = 0;
    if (!parseDimension(positional.at(3), width) ||
        !parseDimension(positional.at(4), height)) {
      std::cerr << "Width and height must be between 1 and "
                << kMaximumDimension << ".\n";
      return 3;
    }
    request.outputSize = QSize(width, height);
  }

  const ExperimentalSvg::RasterizationResult result =
      ExperimentalSvg::RasterizationService::rasterize(request);
  if (!result.isValid()) {
    std::cerr << result.error.toLocal8Bit().constData() << "\n";
    return 4;
  }

  const QString outputPath = positional.at(2);
  if (!result.image.save(outputPath, "PNG")) {
    std::cerr << "Unable to write PNG output: "
              << outputPath.toLocal8Bit().constData() << "\n";
    return 5;
  }

  std::cout << "Rasterized " << request.sourcePath.toLocal8Bit().constData()
            << " to " << result.image.width() << "x" << result.image.height()
            << " at " << outputPath.toLocal8Bit().constData() << "\n";
  return 0;
}
