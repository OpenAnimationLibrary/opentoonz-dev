#include "toonz/toonzscene.h"

#ifdef _WIN32

#include "../toonz/svglevelloader.h"

#include <QApplication>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>

namespace {

enum class SvgOpenMode {
  RetainSvg,
  ConvertToToonzVector,
  Cancel
};

SvgOpenMode askSvgOpenMode() {
  QMessageBox dialog(QApplication::activeWindow());
  dialog.setIcon(QMessageBox::Question);
  dialog.setWindowTitle(QObject::tr("Open SVG"));
  dialog.setText(QObject::tr("How should OpenToonz open this SVG file?"));
  dialog.setInformativeText(QObject::tr(
      "Open as SVG Level keeps the original SVG as the authoritative source "
      "and creates a read-only raster representation for display. Convert to "
      "Toonz Vector Level uses the existing editable SVG-to-PLI conversion "
      "path."));

  QPushButton *retainButton = dialog.addButton(
      QObject::tr("Open as SVG Level"), QMessageBox::AcceptRole);
  QPushButton *convertButton = dialog.addButton(
      QObject::tr("Convert to Toonz Vector Level"), QMessageBox::ActionRole);
  QPushButton *cancelButton =
      dialog.addButton(QObject::tr("Cancel"), QMessageBox::RejectRole);

  dialog.setDefaultButton(retainButton);
  dialog.setEscapeButton(cancelButton);
  dialog.exec();

  if (dialog.clickedButton() == retainButton) return SvgOpenMode::RetainSvg;
  if (dialog.clickedButton() == convertButton)
    return SvgOpenMode::ConvertToToonzVector;
  return SvgOpenMode::Cancel;
}

TXshLevel *loadLevelWithSvgChoice(ToonzScene *scene,
                                  const TFilePath &actualPath,
                                  const LevelOptions *levelOptions,
                                  std::wstring levelName,
                                  const std::vector<TFrameId> &fIds) {
  // The foundation needs a minimal user path into retained SVG levels, but
  // scene resource loading must remain non-interactive. The choice is offered
  // only when an SVG is explicitly opened outside scene loading.
  if (!scene->isLoading() && actualPath.getType() == "svg") {
    switch (askSvgOpenMode()) {
    case SvgOpenMode::RetainSvg:
      return SvgLevel::loadRetainedLevel(scene, actualPath, levelName);
    case SvgOpenMode::Cancel:
      return nullptr;
    case SvgOpenMode::ConvertToToonzVector:
      break;
    }
  }

  return scene->loadLevel(actualPath, levelOptions, levelName, fIds);
}

}  // namespace

TXshLevel *ToonzScene::loadLevel(const TFilePath &actualPath) {
  return loadLevelWithSvgChoice(this, actualPath, nullptr, L"",
                                std::vector<TFrameId>());
}

TXshLevel *ToonzScene::loadLevel(const TFilePath &actualPath,
                                 const LevelOptions *levelOptions) {
  return loadLevelWithSvgChoice(this, actualPath, levelOptions, L"",
                                std::vector<TFrameId>());
}

TXshLevel *ToonzScene::loadLevel(const TFilePath &actualPath,
                                 const LevelOptions *levelOptions,
                                 std::wstring levelName) {
  return loadLevelWithSvgChoice(this, actualPath, levelOptions, levelName,
                                std::vector<TFrameId>());
}

#endif
