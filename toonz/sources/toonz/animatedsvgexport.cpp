#include "ocaio.h"

#include "toonz/tcamera.h"
#include "toonz/scenefx.h"
#include "toonz/sceneproperties.h"
#include "toonz/toonzscene.h"
#include "toonz/tproject.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonz/tscenehandle.h"
#include "tapp.h"
#include "menubarcommandids.h"
#include "filebrowserpopup.h"
#include "timage_io.h"
#include "toutputproperties.h"
#include "trenderer.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QTemporaryDir>
#include <QUrl>
#include <QWaitCondition>
#include <QXmlStreamWriter>

namespace {

struct SvgExportData {
  QJsonArray layers;
  QMap<QString, OCAIo::OCAAsset> assets;
  double frameRate = 24.0;
  int frameCount   = 0;
  int width        = 0;
  int height       = 0;
};

QString assetHref(const OCAIo::OCAAsset &asset, const TFilePath &imageDir,
                  bool embedImages) {
  QString fileName = QFileInfo(asset.fileName).fileName();
  if (!embedImages)
    return imageDir.withoutParentDir().getQString() + "/" + fileName;

  QFile file(QDir(imageDir.getQString()).filePath(fileName));
  if (!file.open(QIODevice::ReadOnly)) return QString();
  QString mime = asset.fileExt == "svg" ? "image/svg+xml" : "image/png";
  return QString("data:%1;base64,%2")
      .arg(mime, QString::fromLatin1(file.readAll().toBase64()));
}

void writeLayer(QXmlStreamWriter &xml, const QJsonObject &layer,
                const SvgExportData &data,
                const QMap<QString, QString> &assetIds, int &layerId) {
  QJsonArray childLayers = layer["childLayers"].toArray();
  if (!childLayers.isEmpty()) {
    xml.writeStartElement("g");
    xml.writeAttribute("id", QString("group_%1").arg(++layerId));
    for (const QJsonValue &child : childLayers)
      writeLayer(xml, child.toObject(), data, assetIds, layerId);
    xml.writeEndElement();
    return;
  }

  QJsonArray frames = layer["frames"].toArray();
  if (frames.isEmpty()) return;

  QStringList values;
  QStringList keyTimes;
  QString firstHref;
  bool firstValue = true;
  int elapsed     = 0;
  for (const QJsonValue &value : frames) {
    QJsonObject frame = value.toObject();
    keyTimes.append(
        QString::number(double(elapsed) / data.frameCount, 'g', 12));
    QString href     = "#blank_frame";
    QString fileName = QFileInfo(frame["fileName"].toString()).fileName();
    if (assetIds.contains(fileName)) {
      href = "#" + assetIds.value(fileName);
    }
    values.append(href);
    if (firstValue) {
      firstHref  = href;
      firstValue = false;
    }
    elapsed += frame["duration"].toInt(1);
  }
  keyTimes.append("1");
  values.append(values.last());

  xml.writeStartElement("use");
  xml.writeAttribute("id", QString("layer_%1").arg(++layerId));
  xml.writeAttribute("opacity",
                     QString::number(layer["opacity"].toDouble(1.0)));
  xml.writeAttribute("href", firstHref);

  xml.writeStartElement("animate");
  xml.writeAttribute("attributeName", "href");
  xml.writeAttribute("calcMode", "discrete");
  xml.writeAttribute("values", values.join(';'));
  xml.writeAttribute("keyTimes", keyTimes.join(';'));
  xml.writeAttribute(
      "dur", QString::number(data.frameCount / data.frameRate, 'g', 12) + "s");
  xml.writeAttribute("repeatCount", "indefinite");
  xml.writeEndElement();
  xml.writeEndElement();
}

bool writeAnimatedSvg(const TFilePath &svgPath, const TFilePath &imageDir,
                      const SvgExportData &data, bool embedImages) {
  QFile file(svgPath.getQString());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

  QXmlStreamWriter xml(&file);
  xml.setAutoFormatting(true);
  xml.writeStartDocument();
  xml.writeStartElement("svg");
  xml.writeDefaultNamespace("http://www.w3.org/2000/svg");
  xml.writeAttribute("width", QString::number(data.width));
  xml.writeAttribute("height", QString::number(data.height));
  xml.writeAttribute("viewBox",
                     QString("0 0 %1 %2").arg(data.width).arg(data.height));
  xml.writeStartElement("desc");
  xml.writeCharacters("Animated SVG exported by OpenToonz");
  xml.writeEndElement();

  QMap<QString, QString> assetIds;
  xml.writeStartElement("defs");
  xml.writeEmptyElement("g");
  xml.writeAttribute("id", "blank_frame");
  int assetNumber = 0;
  for (const OCAIo::OCAAsset &asset : data.assets) {
    QString fileName = QFileInfo(asset.fileName).fileName();
    QString id       = QString("asset_%1").arg(++assetNumber);
    assetIds.insert(fileName, id);
    xml.writeEmptyElement("image");
    xml.writeAttribute("id", id);
    xml.writeAttribute("x", QString::number((data.width - asset.width) / 2));
    xml.writeAttribute("y", QString::number((data.height - asset.height) / 2));
    xml.writeAttribute("width", QString::number(asset.width));
    xml.writeAttribute("height", QString::number(asset.height));
    xml.writeAttribute("preserveAspectRatio", "none");
    xml.writeAttribute("href", assetHref(asset, imageDir, embedImages));
  }
  xml.writeEndElement();

  int layerId = 0;
  for (const QJsonValue &value : data.layers)
    writeLayer(xml, value.toObject(), data, assetIds, layerId);

  xml.writeEndElement();
  xml.writeEndDocument();
  return !xml.hasError();
}

SvgExportData fromOcaData(const OCAIo::OCAData &ocaData) {
  SvgExportData data;
  data.layers     = ocaData.layers();
  data.assets     = ocaData.assets();
  data.frameRate  = ocaData.frameRate();
  data.frameCount = ocaData.frameCount();
  data.width      = ocaData.width();
  data.height     = ocaData.height();
  return data;
}

class SceneRenderPort final : public TRenderPort {
  TFilePath m_imageDir;
  QMap<QByteArray, QString> m_hashes;
  int m_assetNumber = 0;

public:
  bool finished = false;
  bool failed   = false;
  QMap<int, QString> frameAssets;
  QMap<QString, OCAIo::OCAAsset> assets;

  SceneRenderPort(const TFilePath &imageDir, const TDimension &resolution)
      : m_imageDir(imageDir) {
    double rx = resolution.lx * 0.5;
    double ry = resolution.ly * 0.5;
    setRenderArea(TRectD(-rx, -ry, rx, ry));
  }

  void onRenderRasterCompleted(const RenderData &renderData) override {
    if (!renderData.m_rasA) {
      failed = true;
      return;
    }

    QString fileName =
        QString("rendered_%1.png").arg(++m_assetNumber, 4, 10, QChar('0'));
    TFilePath filePath = m_imageDir + TFilePath(fileName);
    try {
      TRasterImageP image(renderData.m_rasA->clone());
      TImageWriterP writer(filePath);
      writer->save(image);

      QFile file(filePath.getQString());
      if (!file.open(QIODevice::ReadOnly)) {
        failed = true;
        return;
      }
      QByteArray hash =
          QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
      file.close();

      if (m_hashes.contains(hash)) {
        QFile::remove(filePath.getQString());
        fileName = m_hashes.value(hash);
      } else {
        m_hashes.insert(hash, fileName);
        OCAIo::OCAAsset asset;
        asset.width    = image->getRaster()->getLx();
        asset.height   = image->getRaster()->getLy();
        asset.fileName = fileName;
        asset.fileExt  = "png";
        assets.insert(fileName, asset);
      }
      for (double frame : renderData.m_frames)
        frameAssets.insert(qRound(frame), fileName);
    } catch (...) {
      failed = true;
    }
  }

  void onRenderFailure(const RenderData &, TException &) override {
    failed = true;
  }

  void onRenderFinished(bool isCanceled = false) override {
    failed   = failed || isCanceled;
    finished = true;
  }
};

bool renderScene(ToonzScene *scene, TXsheet *xsheet, const TFilePath &imageDir,
                 SvgExportData &data) {
  TOutputProperties *properties = scene->getProperties()->getOutputProperties();
  TRenderSettings settings      = properties->getRenderSettings();
  TDimension cameraRes          = scene->getCurrentCamera()->getRes();
  int shrink                    = qMax(1, settings.m_shrinkX);

  SceneRenderPort port(imageDir, cameraRes);
  TRenderer renderer(1);
  renderer.enablePrecomputing(false);
  renderer.addPort(&port);

  auto *renderData = new std::vector<TRenderer::RenderData>;
  for (int frame = 0; frame < xsheet->getFrameCount(); ++frame) {
    TFxPair fxPair;
    fxPair.m_frameA = ::buildSceneFx(
        scene, xsheet, frame, TOutputProperties::AllLevels, shrink, false);
    renderData->push_back(TRenderer::RenderData(frame, settings, fxPair));
  }

  QMutex mutex;
  mutex.lock();
  renderer.startRendering(renderData);
  while (!port.finished) {
    QCoreApplication::processEvents();
    QWaitCondition waitCondition;
    waitCondition.wait(&mutex, 100);
  }
  mutex.unlock();
  renderer.removePort(&port);
  if (port.failed || port.frameAssets.isEmpty()) return false;

  QJsonArray frames;
  for (int frame = 0; frame < xsheet->getFrameCount();) {
    QString fileName = port.frameAssets.value(frame);
    int duration     = 1;
    while (frame + duration < xsheet->getFrameCount() &&
           port.frameAssets.value(frame + duration) == fileName)
      ++duration;

    QJsonObject frameObject;
    frameObject["fileName"] = fileName;
    frameObject["duration"] = duration;
    frames.append(frameObject);
    frame += duration;
  }
  QJsonObject layer;
  layer["frames"]      = frames;
  layer["childLayers"] = QJsonArray();
  layer["opacity"]     = 1.0;
  data.layers.append(layer);
  data.assets     = port.assets;
  data.frameRate  = properties->getFrameRate();
  data.frameCount = xsheet->getFrameCount();
  data.width      = cameraRes.lx / shrink;
  data.height     = cameraRes.ly / qMax(1, settings.m_shrinkY);
  return true;
}

class ExportAnimatedSVGCommand final : public MenuItemHandler {
public:
  ExportAnimatedSVGCommand() : MenuItemHandler(MI_ExportAnimatedSVG) {}
  void execute() override;
};

void ExportAnimatedSVGCommand::execute() {
  ToonzScene *scene = TApp::instance()->getCurrentScene()->getScene();
  TXsheet *xsheet   = TApp::instance()->getCurrentXsheet()->getXsheet();
  TFilePath fp      = scene->getScenePath().withType("svg");

  static GenericSaveFilePopup *savePopup       = nullptr;
  static QCheckBox *rasterizeVectors           = nullptr;
  static QCheckBox *renderSceneResult          = nullptr;
  static QCheckBox *embedImages                = nullptr;
  static QCheckBox *openInBrowser              = nullptr;
  static DVGui::ProgressDialog *progressDialog = nullptr;

  if (!savePopup) {
    QWidget *customWidget = new QWidget();
    rasterizeVectors      = new QCheckBox(QObject::tr("Rasterize Vectors"));
    renderSceneResult =
        new QCheckBox(QObject::tr("Render Scene Before Export"));
    embedImages   = new QCheckBox(QObject::tr("Embed Images in SVG"));
    openInBrowser = new QCheckBox(QObject::tr("Open in Browser When Complete"));
    rasterizeVectors->setChecked(true);
    openInBrowser->setChecked(true);
    rasterizeVectors->setToolTip(QObject::tr(
        "Checked: vector drawings are exported as PNG images\nUnchecked: "
        "vector drawings are exported as SVG images"));
    renderSceneResult->setToolTip(QObject::tr(
        "Render the complete scene to PNG frames so animation, camera moves, "
        "compositing, and effects are included"));
    embedImages->setToolTip(QObject::tr(
        "Store image data as base64 inside the animated SVG instead of "
        "creating an external image folder"));

    QGridLayout *layout = new QGridLayout(customWidget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);
    layout->addWidget(rasterizeVectors, 0, 0);
    layout->addWidget(renderSceneResult, 1, 0);
    layout->addWidget(embedImages, 2, 0);
    layout->addWidget(openInBrowser, 3, 0);
    QObject::connect(renderSceneResult, &QCheckBox::toggled, rasterizeVectors,
                     &QCheckBox::setDisabled);

    progressDialog = new DVGui::ProgressDialog("", QObject::tr("Hide"), 0, 0);
    progressDialog->setWindowTitle(QObject::tr("Exporting..."));
    progressDialog->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);
    progressDialog->setWindowModality(Qt::WindowModal);

    savePopup = new GenericSaveFilePopup(QObject::tr("Export Animated SVG"),
                                         customWidget);
    savePopup->addFilterType("svg");
  }

  if (!scene->isUntitled())
    savePopup->setFolder(fp.getParentDir());
  else
    savePopup->setFolder(
        TProjectManager::instance()->getCurrentProject()->getScenesPath());
  savePopup->setFilename(fp.withoutParentDir());
  fp = savePopup->getPath();
  if (fp.isEmpty()) return;

  TDimension resolution = scene->getCurrentCamera()->getRes();
  double estimatedMb    = double(resolution.lx) * resolution.ly *
                       xsheet->getFrameCount() * 4.0 * 4.0 / 3.0 /
                       (1024.0 * 1024.0);
  if (embedImages->isChecked() && estimatedMb >= 64.0) {
    QString warning = QObject::tr(
                          "Embedding this animation may create an SVG as large "
                          "as approximately %1 MB. Continue?")
                          .arg(qRound64(estimatedMb));
    std::vector<QString> buttons = {QObject::tr("Cancel"),
                                    QObject::tr("Export")};
    if (DVGui::MsgBox(DVGui::WARNING, warning, buttons) != 2) return;
  }

  QTemporaryDir temporaryImageDir;
  TFilePath imageDir =
      embedImages->isChecked()
          ? TFilePath(temporaryImageDir.path())
          : fp.getParentDir() + TFilePath(fp.getName() + "_files");
  QDir imageDirectory(imageDir.getQString());
  if (!imageDirectory.exists() && !imageDirectory.mkpath(".")) {
    DVGui::error(QObject::tr("Unable to create the image folder."));
    return;
  }

  progressDialog->setLabelText(QObject::tr("Exporting image assets..."));
  progressDialog->setValue(0);
  progressDialog->show();
  QCoreApplication::processEvents();

  SvgExportData data;
  if (renderSceneResult->isChecked()) {
    progressDialog->setLabelText(QObject::tr("Rendering scene frames..."));
    if (!renderScene(scene, xsheet, imageDir, data)) {
      progressDialog->close();
      DVGui::error(QObject::tr("Unable to render the scene for SVG export."));
      return;
    }
  } else {
    OCAIo::OCAData ocaData;
    ocaData.setProgressDialog(progressDialog);
    ocaData.build(scene, xsheet, QString::fromStdString(fp.getName()),
                  imageDir.getQString(), false, !rasterizeVectors->isChecked(),
                  false);
    if (ocaData.isEmpty()) {
      progressDialog->close();
      DVGui::error(QObject::tr("No columns can be exported."));
      return;
    }
    data = fromOcaData(ocaData);
  }
  if (!writeAnimatedSvg(fp, imageDir, data, embedImages->isChecked())) {
    progressDialog->close();
    DVGui::error(QObject::tr("Unable to save the animated SVG file."));
    return;
  }

  progressDialog->close();

  if (openInBrowser->isChecked())
    QDesktopServices::openUrl(QUrl::fromLocalFile(fp.getQString()));
  else
    DVGui::info(QObject::tr("Animated SVG exported successfully."));
}

ExportAnimatedSVGCommand exportAnimatedSVGCommand;

}  // namespace
