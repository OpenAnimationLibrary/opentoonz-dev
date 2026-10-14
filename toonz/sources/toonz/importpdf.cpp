#include "importpdf.h"
#include "thirdparty.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <map>

namespace {

constexpr int MinimumDpi  = 36;
constexpr int StandardDpi = 120;
constexpr int MaximumDpi  = 1200;

QString availableLevelName(const QDir &destination, const QString &baseName) {
  for (int suffix = 1;; ++suffix) {
    const QString name =
        suffix == 1 ? baseName : QString("%1_%2").arg(baseName).arg(suffix);
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
      pages.emplace(match.captured(1).toInt(),
                    directory.absoluteFilePath(file));
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
      error =
          QObject::tr("Could not read converted PDF page %1.").arg(index + 1);
      break;
    }

    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    const QString fileName =
        QString("%1.%2.png").arg(levelName).arg(index + 1, 4, 10, QChar('0'));
    const QString outputPath = destination.absoluteFilePath(fileName);
    if (!image.save(outputPath, "PNG")) {
      error =
          QObject::tr("Could not save imported PDF page %1.").arg(index + 1);
      break;
    }
    outputPages.append(outputPath);
  }

  if (error.isEmpty()) return true;
  for (const QString &path : outputPages) QFile::remove(path);
  outputPages.clear();
  return false;
}

}  // namespace

bool convertPdfToPngLevel(const TFilePath &sourcePath, TFilePath &levelPath) {
  QWidget *parent = QApplication::activeWindow();
  if (!ThirdParty::checkPdfRenderer()) {
    QMessageBox::warning(
        parent, QObject::tr("Load PDF"),
        QObject::tr("PDF loading requires Poppler's pdftoppm. Set its "
                    "folder in Preferences > Encoder/Decoder."));
    return false;
  }

  const QString source = sourcePath.getQString();

  const QString destinationPath = QFileDialog::getExistingDirectory(
      parent, QObject::tr("Choose PDF Import Destination"),
      QFileInfo(source).absolutePath());
  if (destinationPath.isEmpty()) return false;

  bool accepted = false;
  const int dpi = QInputDialog::getInt(
      parent, QObject::tr("PDF Load Resolution"),
      QObject::tr("Rasterization DPI (72 low, 120 standard, 300 high):"),
      StandardDpi, MinimumDpi, MaximumDpi, 1, &accepted);
  if (!accepted) return false;

  const int firstPage = QInputDialog::getInt(
      parent, QObject::tr("PDF Page Range"), QObject::tr("First page:"), 1, 1,
      99999, 1, &accepted);
  if (!accepted) return false;
  const int lastPage =
      QInputDialog::getInt(parent, QObject::tr("PDF Page Range"),
                           QObject::tr("Last page (0 loads through the end):"),
                           0, 0, 99999, 1, &accepted);
  if (!accepted) return false;
  if (lastPage != 0 && lastPage < firstPage) {
    QMessageBox::warning(
        parent, QObject::tr("Load PDF"),
        QObject::tr("The last page cannot precede the first page."));
    return false;
  }

  QTemporaryDir temporary;
  if (!temporary.isValid()) {
    QMessageBox::warning(parent, QObject::tr("Import PDF"),
                         QObject::tr("Could not create a temporary folder."));
    return false;
  }

  const QString prefix = "pdf_page";
  QString error;
  QProcess process;
  QStringList rendererArguments{"-png", "-r", QString::number(dpi), "-f",
                                QString::number(firstPage)};
  if (lastPage != 0) rendererArguments << "-l" << QString::number(lastPage);
  rendererArguments << source << temporary.path() + QDir::separator() + prefix;
  ThirdParty::runPdfRenderer(process, rendererArguments);
  if (!process.waitForStarted()) {
    QMessageBox::warning(parent, QObject::tr("Import PDF"),
                         QObject::tr("Could not start the PDF renderer."));
    return false;
  }

  QApplication::setOverrideCursor(Qt::WaitCursor);
  const bool rendererFinished = process.waitForFinished(-1);
  QApplication::restoreOverrideCursor();
  if (!rendererFinished) {
    process.kill();
    process.waitForFinished();
    QMessageBox::warning(parent, QObject::tr("Import PDF"),
                         QObject::tr("PDF conversion did not complete."));
    return false;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (error.isEmpty()) error = QObject::tr("PDF conversion failed.");
    QMessageBox::warning(parent, QObject::tr("Import PDF"), error);
    return false;
  }

  const QStringList pages = renderedPages(QDir(temporary.path()), prefix);
  if (pages.isEmpty()) {
    QMessageBox::warning(parent, QObject::tr("Import PDF"),
                         QObject::tr("PDF conversion produced no pages."));
    return false;
  }

  const QDir destination(destinationPath);
  const QString levelName =
      availableLevelName(destination, QFileInfo(source).completeBaseName());
  QStringList outputPages;
  if (!convertPages(pages, destination, levelName, dpi, outputPages, error)) {
    QMessageBox::warning(parent, QObject::tr("Import PDF"), error);
    return false;
  }
  levelPath = TFilePath(outputPages.front().toStdWString());
  return true;
}
