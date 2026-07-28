#include "svg_open_mode_dialog.h"

#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>

namespace ExperimentalSvg {

OpenMode OpenModeDialog::ask(QWidget *parent) {
  QMessageBox dialog(parent);
  dialog.setIcon(QMessageBox::Question);
  dialog.setWindowTitle(QObject::tr("Open SVG"));
  dialog.setText(QObject::tr("How should OpenToonz handle this SVG file?"));
  dialog.setInformativeText(QObject::tr(
      "Experimental SVG Levels preserve the SVG source and are initially "
      "read-only. Converting creates an editable Toonz Vector Level, but some "
      "SVG features may be approximated or omitted."));

  QPushButton *openSvgButton = dialog.addButton(
      QObject::tr("Open as SVG Level (Experimental)"), QMessageBox::AcceptRole);
  QPushButton *convertButton = dialog.addButton(
      QObject::tr("Convert to Toonz Vector Level"), QMessageBox::ActionRole);
  QPushButton *cancelButton =
      dialog.addButton(QObject::tr("Cancel"), QMessageBox::RejectRole);

  dialog.setDefaultButton(openSvgButton);
  dialog.setEscapeButton(cancelButton);
  dialog.exec();

  QAbstractButton *clicked = dialog.clickedButton();
  if (clicked == openSvgButton) return OpenMode::OpenExperimentalSvgLevel;
  if (clicked == convertButton) return OpenMode::ConvertToToonzVector;
  return OpenMode::Cancel;
}

}  // namespace ExperimentalSvg
