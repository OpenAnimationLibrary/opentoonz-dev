#include "iocommand.h"
#include "menubarcommandids.h"
#include "tapp.h"

#include "toonz/txshlevelhandle.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"
#include "toonzqt/gutil.h"
#include "toonzqt/menubarcommand.h"

#include "tfilepath.h"
#include "trasterimage.h"
#include "trop.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>
#include <map>
#include <vector>

namespace {

constexpr int MinimumDpi  = 36;
constexpr int StandardDpi = 120;
constexpr int MaximumDpi  = 1200;

QString findPdfRenderer() {
#ifdef _WIN32
  const QString executable = "pdftoppm.exe";
#else
  const QString executable = "pdftoppm";
#endif

  const QString appDir = QCoreApplication::applicationDirPath();
  const QStringList candidates = {
      appDir + QDir::separator() + executable,
      appDir + QDir::separator() + "poppler" + QDir::separator() + executable,
      appDir + QDir::separator() + "poppler" + QDir::separator() + "bin" +
          QDir::separator() + executable,
      QStandardPaths::findExecutable(executable)};

  for (const QString &candidate : candidates) {
    if (!candidate.isEmpty() && QFileInfo::exists(candidate)) return candidate;
  }
  return QString();
}

QString availableLevelName(const QDir &destination) {
  for (int suffix = 1;; ++suffix) {
    const QString name = suffix == 1 ? "page" : QString("page_%1").arg(suffix);
    if (destination.entryList({name + ".*.png"}, QDir::Files).isEmpty())
      return name;
  }
}

QStringList renderedPages(const QDir &directory, const QString &prefix) {
  const QRegularExpression pattern("^" + QRegularExpression::escape(prefix) +
                                   "-(\\d+)\\.png$");
  std::map<int, QString> pages;
  const QStringList files =
      directory.entryList({prefix + "-*.png"}, QDir::Files, QDir::Name);
  for (const QString &file : files) {
    const QRegularExpressionMatch match = pattern.match(file);
    if (match.hasMatch())
      pages.emplace(match.captured(1).toInt(), directory.absoluteFilePath(file));
  }

  QStringList result;
  for (const auto &page : pages) result.append(page.second);
  return result;
}

bool convertPages(const QStringList &sourcePages, const QDir &destination,
                  const QString &levelName, int dpi, QStringList &outputPages,
                  QString &error) {
  const int dotsPerMeter = qRound(dpi / 0.0254);
  for (int index = 0; index < sourcePages.size(); ++index) {
    QImage image(sourcePages.at(index));
    if (image.isNull()) {
      error = QObject::tr("Could not read converted PDF page %1.")
                  .arg(index + 1);
      break;
    }

    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    const QString fileName =
        QString("%1.%2.png").arg(levelName).arg(index + 1, 4, 10, QChar('0'));
    const QString outputPath = destination.absoluteFilePath(fileName);
    if (!image.save(outputPath, "PNG")) {
      error = QObject::tr("Could not save imported PDF page %1.")
                  .arg(index + 1);
      break;
    }
    outputPages.append(outputPath);
  }

  if (error.isEmpty()) return true;
  for (const QString &path : outputPages) QFile::remove(path);
  outputPages.clear();
  return false;
}

QImage levelFrameToQImage(TXshSimpleLevel *level, const TFrameId &fid,
                          int pageNumber, QString &error) {
  TRasterImageP rasterImage = (TRasterImageP)level->getFrame(fid, false);
  if (!rasterImage || !rasterImage->getRaster()) {
    error = QObject::tr("Could not read raster level frame %1.")
                .arg(pageNumber);
    return QImage();
  }

  TRasterP raster     = rasterImage->getRaster();
  TRaster32P raster32 = raster;
  if (!raster32) {
    raster32 = TRaster32P(raster->getSize());
    TRop::convert(raster32, raster);
  }

  QImage image = rasterToQImage(raster32);
  if (image.isNull())
    error = QObject::tr("Could not convert raster level frame %1.")
                .arg(pageNumber);
  return image;
}

QPageLayout pageLayoutForImage(const QImage &image, const TPointD &dpi) {
  const double dpiX =
      std::isfinite(dpi.x) && dpi.x > 0.0 ? dpi.x : StandardDpi;
  const double dpiY =
      std::isfinite(dpi.y) && dpi.y > 0.0 ? dpi.y : StandardDpi;
  const double widthMm  = image.width() * 25.4 / dpiX;
  const double heightMm = image.height() * 25.4 / dpiY;
  const bool landscape  = widthMm > heightMm;
  const QSizeF portraitSize = landscape ? QSizeF(heightMm, widthMm)
                                        : QSizeF(widthMm, heightMm);
  return QPageLayout(QPageSize(portraitSize, QPageSize::Millimeter),
                     landscape ? QPageLayout::Landscape
                               : QPageLayout::Portrait,
                     QMarginsF(), QPageLayout::Millimeter);
}

bool exportLevelToPdf(TXshSimpleLevel *level, const QString &outputPath,
                      QString &error) {
  const std::vector<TFrameId> fids = level->getFids();
  if (fids.empty()) {
    error = QObject::tr("The current raster level contains no frames.");
    return false;
  }

  QImage page = levelFrameToQImage(level, fids.front(), 1, error);
  if (page.isNull()) return false;

  QSaveFile output(outputPath);
  if (!output.open(QIODevice::WriteOnly)) {
    error = QObject::tr("Could not create %1: %2")
                .arg(outputPath, output.errorString());
    return false;
  }

  bool completed = false;
  {
    QPdfWriter writer(&output);
    writer.setCreator(QCoreApplication::applicationName());
    writer.setTitle(QString::fromStdWString(level->getName()));
    writer.setResolution(StandardDpi);
    if (!writer.setPageLayout(pageLayoutForImage(
            page, level->getDpi(fids.front())))) {
      error = QObject::tr("Could not set the PDF page size.");
    } else {
      QPainter painter(&writer);
      if (!painter.isActive()) {
        error = QObject::tr("Could not start writing the PDF.");
      } else {
        painter.drawImage(QRect(0, 0, writer.width(), writer.height()), page);

        for (int index = 1; index < static_cast<int>(fids.size()); ++index) {
          page = levelFrameToQImage(level, fids[index], index + 1, error);
          if (page.isNull()) break;

          if (!writer.setPageLayout(
                  pageLayoutForImage(page, level->getDpi(fids[index]))) ||
              !writer.newPage()) {
            error = QObject::tr("Could not create PDF page %1.")
                        .arg(index + 1);
            break;
          }
          painter.drawImage(QRect(0, 0, writer.width(), writer.height()), page);
        }
        painter.end();
        completed = error.isEmpty();
      }
    }
  }

  if (!completed) {
    output.cancelWriting();
    return false;
  }
  if (!output.commit()) {
    error = QObject::tr("Could not save %1: %2")
                .arg(outputPath, output.errorString());
    return false;
  }
  return true;
}

class ImportPdfCommand final : public MenuItemHandler {
public:
  ImportPdfCommand() : MenuItemHandler(MI_ImportPDF) {}

  void execute() override {
    QWidget *parent = QApplication::activeWindow();
    const QString renderer = findPdfRenderer();
    if (renderer.isEmpty()) {
      QMessageBox::warning(
          parent, QObject::tr("Import PDF"),
          QObject::tr("PDF import currently requires Poppler's pdftoppm. "
                      "Place it beside OpenToonz, under poppler/bin, or on "
                      "the system PATH."));
      return;
    }

    const QString source = QFileDialog::getOpenFileName(
        parent, QObject::tr("Import PDF"), QString(),
        QObject::tr("PDF Documents (*.pdf)"));
    if (source.isEmpty()) return;

    bool accepted = false;
    const int dpi  = QInputDialog::getInt(
        parent, QObject::tr("PDF Import Resolution"),
        QObject::tr("Rasterization DPI (72 low, 120 standard, 300 high):"),
        StandardDpi, MinimumDpi, MaximumDpi, 1, &accepted);
    if (!accepted) return;

    const QString destinationPath = QFileDialog::getExistingDirectory(
        parent, QObject::tr("Choose PDF Import Destination"),
        QFileInfo(source).absolutePath());
    if (destinationPath.isEmpty()) return;

    QTemporaryDir temporary;
    if (!temporary.isValid()) {
      QMessageBox::warning(parent, QObject::tr("Import PDF"),
                           QObject::tr("Could not create a temporary folder."));
      return;
    }

    const QString prefix = "pdf_page";
    QString error;
    QProcess process;
    process.start(
        renderer,
        QStringList{"-png", "-r", QString::number(dpi), source,
                    temporary.path() + QDir::separator() + prefix});
    if (!process.waitForStarted()) {
      QMessageBox::warning(parent, QObject::tr("Import PDF"),
                           QObject::tr("Could not start the PDF renderer."));
      return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool rendererFinished = process.waitForFinished(-1);
    QApplication::restoreOverrideCursor();
    if (!rendererFinished) {
      process.kill();
      process.waitForFinished();
      QMessageBox::warning(parent, QObject::tr("Import PDF"),
                           QObject::tr("PDF conversion did not complete."));
      return;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
      error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
      if (error.isEmpty()) error = QObject::tr("PDF conversion failed.");
      QMessageBox::warning(parent, QObject::tr("Import PDF"), error);
      return;
    }

    const QStringList pages = renderedPages(QDir(temporary.path()), prefix);
    if (pages.isEmpty()) {
      QMessageBox::warning(parent, QObject::tr("Import PDF"),
                           QObject::tr("PDF conversion produced no pages."));
      return;
    }

    const QDir destination(destinationPath);
    const QString levelName = availableLevelName(destination);
    QStringList outputPages;
    if (!convertPages(pages, destination, levelName, dpi, outputPages, error)) {
      QMessageBox::warning(parent, QObject::tr("Import PDF"), error);
      return;
    }

    IoCmd::LoadResourceArguments args(
        TFilePath(outputPages.front().toStdWString()));
    args.importPolicy = IoCmd::LoadResourceArguments::LOAD;
    if (!IoCmd::loadResources(args)) {
      QMessageBox::warning(
          parent, QObject::tr("Import PDF"),
          QObject::tr("The PDF pages were converted to %1, but OpenToonz "
                      "could not load the resulting image sequence.")
              .arg(destination.absolutePath()));
      return;
    }

    QMessageBox::information(
        parent, QObject::tr("Import PDF"),
        QObject::tr("Imported %1 PDF pages as %2.####.png at %3 DPI.")
            .arg(outputPages.size())
            .arg(levelName)
            .arg(dpi));
  }
} importPdfCommand;

class ExportPdfCommand final : public MenuItemHandler {
public:
  ExportPdfCommand() : MenuItemHandler(MI_ExportPDF) {}

  void execute() override {
    QWidget *parent        = QApplication::activeWindow();
    TXshSimpleLevel *level =
        TApp::instance()->getCurrentLevel()->getSimpleLevel();
    if (!level || level->getType() != OVL_XSHLEVEL) {
      QMessageBox::warning(
          parent, QObject::tr("Export Level as PDF"),
          QObject::tr("Select a full-color raster level to export."));
      return;
    }

    const QString levelName = QString::fromStdWString(level->getName());
    const QString initialPath = QDir(level->getPath().getParentDir().getQString())
                                    .absoluteFilePath(levelName + ".pdf");
    QString outputPath = QFileDialog::getSaveFileName(
        parent, QObject::tr("Export Level as PDF"), initialPath,
        QObject::tr("PDF Documents (*.pdf)"));
    if (outputPath.isEmpty()) return;
    if (!outputPath.endsWith(".pdf", Qt::CaseInsensitive)) outputPath += ".pdf";

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool exported = exportLevelToPdf(level, outputPath, error);
    QApplication::restoreOverrideCursor();
    if (!exported) {
      QMessageBox::warning(parent, QObject::tr("Export Level as PDF"), error);
      return;
    }

    QMessageBox::information(
        parent, QObject::tr("Export Level as PDF"),
        QObject::tr("Exported %1 frames from %2 as a multipage raster PDF.")
            .arg(level->getFrameCount())
            .arg(levelName));
  }
} exportPdfCommand;

}  // namespace
