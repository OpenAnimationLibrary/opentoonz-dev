#include "iocommand.h"
#include "menubarcommandids.h"

#include "toonzqt/menubarcommand.h"

#include "tfilepath.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <map>

namespace {

constexpr int LowDpi         = 72;
constexpr int StandardDpi    = 120;
constexpr int HighQualityDpi = 300;

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

    const QString destinationPath = QFileDialog::getExistingDirectory(
        parent, QObject::tr("Choose PDF Import Destination"),
        QFileInfo(source).absolutePath());
    if (destinationPath.isEmpty()) return;

    const QStringList qualities = {
        QObject::tr("Low (72 DPI)"),
        QObject::tr("Standard (120 DPI)"),
        QObject::tr("High Quality (300 DPI)")};
    bool accepted = false;
    const QString quality = QInputDialog::getItem(
        parent, QObject::tr("PDF Import Resolution"),
        QObject::tr("Resolution:"), qualities, 1, false, &accepted);
    if (!accepted) return;
    const int qualityIndex = qualities.indexOf(quality);
    int dpi                = StandardDpi;
    if (qualityIndex == 0)
      dpi = LowDpi;
    else if (qualityIndex == 2)
      dpi = HighQualityDpi;

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

}  // namespace
