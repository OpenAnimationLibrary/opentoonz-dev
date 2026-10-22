#include "menubarcommandids.h"
#include "tapp.h"

#include "toonz/sceneproperties.h"
#include "toonz/tcamera.h"
#include "toonz/tscenehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txsheethandle.h"
#include "toonzqt/gutil.h"
#include "toonzqt/menubarcommand.h"

#include "trasterimage.h"
#include "trop.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileDialog>
#include <QImage>
#include <QInputDialog>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>

#include <cmath>
#include <vector>

namespace {

constexpr int StandardDpi = 120;

QImage rasterToPdfImage(const TRasterP &raster) {
  if (!raster) return QImage();
  TRaster32P raster32 = raster;
  if (!raster32) {
    raster32 = TRaster32P(raster->getSize());
    TRop::convert(raster32, raster);
  }
  return rasterToQImage(raster32);
}

QPageLayout pageLayoutForImage(const QImage &image, const TPointD &dpi) {
  const double dpiX = std::isfinite(dpi.x) && dpi.x > 0.0 ? dpi.x : StandardDpi;
  const double dpiY = std::isfinite(dpi.y) && dpi.y > 0.0 ? dpi.y : StandardDpi;
  const double widthMm  = image.width() * 25.4 / dpiX;
  const double heightMm = image.height() * 25.4 / dpiY;
  const bool landscape  = widthMm > heightMm;
  const QSizeF portraitSize =
      landscape ? QSizeF(heightMm, widthMm) : QSizeF(widthMm, heightMm);
  return QPageLayout(QPageSize(portraitSize, QPageSize::Millimeter),
                     landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                     QMarginsF(), QPageLayout::Millimeter);
}

bool writePdf(const QString &outputPath, const QString &title,
              const std::vector<QImage> &pages,
              const std::vector<TPointD> &pageDpis, QString &error) {
  if (pages.empty()) {
    error = QObject::tr("There are no frames to export.");
    return false;
  }

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
    writer.setTitle(title);
    writer.setResolution(StandardDpi);
    if (!writer.setPageLayout(
            pageLayoutForImage(pages.front(), pageDpis.front()))) {
      error = QObject::tr("Could not set the PDF page size.");
    } else {
      QPainter painter(&writer);
      if (!painter.isActive()) {
        error = QObject::tr("Could not start writing the PDF.");
      } else {
        for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
          if (i > 0 && (!writer.setPageLayout(
                            pageLayoutForImage(pages[i], pageDpis[i])) ||
                        !writer.newPage())) {
            error = QObject::tr("Could not create PDF page %1.").arg(i + 1);
            break;
          }
          painter.drawImage(QRect(0, 0, writer.width(), writer.height()),
                            pages[i]);
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

bool collectLevelPages(std::vector<QImage> &pages,
                       std::vector<TPointD> &pageDpis, QString &title,
                       QString &error) {
  TXshSimpleLevel *level =
      TApp::instance()->getCurrentLevel()->getSimpleLevel();
  if (!level || level->getType() != OVL_XSHLEVEL) {
    error = QObject::tr("Select a full-color raster level to export.");
    return false;
  }

  const std::vector<TFrameId> fids = level->getFids();
  title                            = QString::fromStdWString(level->getName());
  for (int i = 0; i < static_cast<int>(fids.size()); ++i) {
    TRasterImageP image = (TRasterImageP)level->getFrame(fids[i], false);
    QImage page = image ? rasterToPdfImage(image->getRaster()) : QImage();
    if (page.isNull()) {
      error = QObject::tr("Could not read raster level frame %1.").arg(i + 1);
      return false;
    }
    pages.push_back(page);
    pageDpis.push_back(level->getDpi(fids[i]));
  }
  return true;
}

bool collectScenePages(std::vector<QImage> &pages,
                       std::vector<TPointD> &pageDpis, QString &title,
                       QString &error) {
  ToonzScene *scene    = TApp::instance()->getCurrentScene()->getScene();
  const int frameCount = scene->getFrameCount();
  if (frameCount <= 0) {
    error = QObject::tr("The current scene contains no frames.");
    return false;
  }

  TCamera *camera             = scene->getCurrentCamera();
  const TDimension resolution = camera->getRes();
  const TPointD dpi           = camera->getDpi();
  title = QString::fromStdString(scene->getScenePath().getName());
  if (title.isEmpty()) title = QObject::tr("OpenToonz Scene");

  TXsheet *xsheet = TApp::instance()->getCurrentXsheet()->getXsheet();
  for (int frame = 0; frame < frameCount; ++frame) {
    TRaster32P raster(resolution.lx, resolution.ly);
    raster->fill(scene->getProperties()->getBgColor());
    scene->renderFrame(raster, frame, xsheet);
    QImage page = rasterToPdfImage(raster);
    if (page.isNull()) {
      error = QObject::tr("Could not render scene frame %1.").arg(frame + 1);
      return false;
    }
    pages.push_back(page);
    pageDpis.push_back(dpi);
    QApplication::processEvents();
  }
  return true;
}

class ExportPdfCommand final : public MenuItemHandler {
public:
  ExportPdfCommand() : MenuItemHandler(MI_ExportPDF) {}

  void execute() override {
    QWidget *parent           = QApplication::activeWindow();
    const QStringList sources = {QObject::tr("Selected Raster Level"),
                                 QObject::tr("Entire Scene")};
    bool accepted             = false;
    const QString source      = QInputDialog::getItem(
             parent, QObject::tr("Export PDF"), QObject::tr("Export source:"),
             sources, 0, false, &accepted);
    if (!accepted) return;

    QString outputPath = QFileDialog::getSaveFileName(
        parent, QObject::tr("Export PDF"), QString(),
        QObject::tr("PDF Documents (*.pdf)"));
    if (outputPath.isEmpty()) return;
    if (!outputPath.endsWith(".pdf", Qt::CaseInsensitive)) outputPath += ".pdf";

    QApplication::setOverrideCursor(Qt::WaitCursor);
    std::vector<QImage> pages;
    std::vector<TPointD> pageDpis;
    QString title, error;
    const bool collected =
        source == sources.front()
            ? collectLevelPages(pages, pageDpis, title, error)
            : collectScenePages(pages, pageDpis, title, error);
    const bool exported =
        collected && writePdf(outputPath, title, pages, pageDpis, error);
    QApplication::restoreOverrideCursor();

    if (!exported)
      QMessageBox::warning(parent, QObject::tr("Export PDF"), error);
  }
} exportPdfCommand;

}  // namespace
