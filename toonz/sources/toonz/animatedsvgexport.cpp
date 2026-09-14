#include "ocaio.h"

#include "toonz/tcamera.h"
#include "toonz/toonzscene.h"
#include "toonz/tproject.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonz/tscenehandle.h"
#include "tapp.h"
#include "menubarcommandids.h"
#include "filebrowserpopup.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QTemporaryDir>
#include <QUrl>
#include <QXmlStreamWriter>

namespace {

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
                const OCAIo::OCAData &data,
                const QMap<QString, QString> &assetIds, int frameCount,
                int &layerId) {
  QJsonArray childLayers = layer["childLayers"].toArray();
  if (!childLayers.isEmpty()) {
    xml.writeStartElement("g");
    xml.writeAttribute("id", QString("group_%1").arg(++layerId));
    for (const QJsonValue &child : childLayers)
      writeLayer(xml, child.toObject(), data, assetIds, frameCount, layerId);
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
    keyTimes.append(QString::number(double(elapsed) / frameCount, 'g', 12));
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
      "dur", QString::number(frameCount / data.frameRate(), 'g', 12) + "s");
  xml.writeAttribute("repeatCount", "indefinite");
  xml.writeEndElement();
  xml.writeEndElement();
}

bool writeAnimatedSvg(const TFilePath &svgPath, const TFilePath &imageDir,
                      const OCAIo::OCAData &data, bool embedImages) {
  QFile file(svgPath.getQString());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

  QXmlStreamWriter xml(&file);
  xml.setAutoFormatting(true);
  xml.writeStartDocument();
  xml.writeStartElement("svg");
  xml.writeDefaultNamespace("http://www.w3.org/2000/svg");
  xml.writeAttribute("width", QString::number(data.width()));
  xml.writeAttribute("height", QString::number(data.height()));
  xml.writeAttribute("viewBox",
                     QString("0 0 %1 %2").arg(data.width()).arg(data.height()));
  xml.writeStartElement("desc");
  xml.writeCharacters("Animated SVG exported by OpenToonz");
  xml.writeEndElement();

  QMap<QString, QString> assetIds;
  xml.writeStartElement("defs");
  xml.writeEmptyElement("g");
  xml.writeAttribute("id", "blank_frame");
  int assetNumber = 0;
  for (const OCAIo::OCAAsset &asset : data.assets()) {
    QString fileName = QFileInfo(asset.fileName).fileName();
    QString id       = QString("asset_%1").arg(++assetNumber);
    assetIds.insert(fileName, id);
    xml.writeEmptyElement("image");
    xml.writeAttribute("id", id);
    xml.writeAttribute("x", QString::number((data.width() - asset.width) / 2));
    xml.writeAttribute("y",
                       QString::number((data.height() - asset.height) / 2));
    xml.writeAttribute("width", QString::number(asset.width));
    xml.writeAttribute("height", QString::number(asset.height));
    xml.writeAttribute("preserveAspectRatio", "none");
    xml.writeAttribute("href", assetHref(asset, imageDir, embedImages));
  }
  xml.writeEndElement();

  int layerId = 0;
  for (const QJsonValue &value : data.layers())
    writeLayer(xml, value.toObject(), data, assetIds, data.frameCount(),
               layerId);

  xml.writeEndElement();
  xml.writeEndDocument();
  return !xml.hasError();
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
  static QCheckBox *embedImages                = nullptr;
  static QCheckBox *openInBrowser              = nullptr;
  static DVGui::ProgressDialog *progressDialog = nullptr;

  if (!savePopup) {
    QWidget *customWidget = new QWidget();
    rasterizeVectors      = new QCheckBox(QObject::tr("Rasterize Vectors"));
    embedImages           = new QCheckBox(QObject::tr("Embed Images in SVG"));
    openInBrowser = new QCheckBox(QObject::tr("Open in Browser When Complete"));
    rasterizeVectors->setChecked(true);
    openInBrowser->setChecked(true);
    rasterizeVectors->setToolTip(QObject::tr(
        "Checked: vector drawings are exported as PNG images\nUnchecked: "
        "vector drawings are exported as SVG images"));
    embedImages->setToolTip(QObject::tr(
        "Store image data as base64 inside the animated SVG instead of "
        "creating an external image folder"));

    QGridLayout *layout = new QGridLayout(customWidget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);
    layout->addWidget(rasterizeVectors, 0, 0);
    layout->addWidget(embedImages, 1, 0);
    layout->addWidget(openInBrowser, 2, 0);

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

  OCAIo::OCAData data;
  data.setProgressDialog(progressDialog);
  data.build(scene, xsheet, QString::fromStdString(fp.getName()),
             imageDir.getQString(), false, !rasterizeVectors->isChecked(),
             false);
  if (data.isEmpty()) {
    progressDialog->close();
    DVGui::error(QObject::tr("No columns can be exported."));
    return;
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
