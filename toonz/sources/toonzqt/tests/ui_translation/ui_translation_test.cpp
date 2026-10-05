#include "toonzqt/uitranslation.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QMessageBox>
#include <QAbstractButton>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTranslator>
#include <iostream>

namespace {

int failures = 0;

void check(bool result, const char *message) {
  if (!result) {
    std::cerr << message << '\n';
    ++failures;
  }
}

QImage render(const QFont &font) {
  QImage image(300, 80, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.setFont(font);
  painter.setPen(Qt::black);
  painter.drawText(5, 50, QStringLiteral("OpenToonz 123"));
  return image;
}

}  // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  if (argc != 2) return 2;
  const QDir catalogs(QString::fromLocal8Bit(argv[1]));
  QTranslator spanish;
  check(spanish.load("toonzqt", catalogs.filePath("spanish")),
        "Spanish UI catalog did not load");
  app.installTranslator(&spanish);

  check(DVGui::fontStyleDisplayName("Bold") == QString::fromUtf8("Negrita"),
        "Known style was not translated");
  check(DVGui::fontStyleDisplayName("Foundry Bold Display") ==
            QStringLiteral("Foundry Bold Display"),
        "Unknown style was guessed from a substring");
  check(DVGui::fontStyleDisplayName("SemiBold") ==
            DVGui::fontStyleDisplayName("Semibold"),
        "Whole-name alias was not recognized");

  QComboBox combo;
  int signalCount = 0;
  QObject::connect(
      &combo,
      static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
      [&signalCount](int) { ++signalCount; });
  const QStringList styles = {"Regular", "Normal", "Bold",
                              "Foundry Bold Display"};
  DVGui::populateFontStyleCombo(&combo, styles, "Bold");
  check(signalCount == 0, "Rebuilding the combo emitted edit signals");
  check(combo.currentText() == QStringLiteral("Negrita") &&
            combo.currentData().toString() == QStringLiteral("Bold"),
        "Translated label replaced the original selected style");
  check(combo.itemText(0) != combo.itemText(1),
        "Different faces with equal translations became indistinguishable");
  check(combo.itemData(2, Qt::ToolTipRole).toString() == QStringLiteral("Bold"),
        "Original font name is not available in the tooltip");

  QTemporaryDir settingsDir;
  QSettings settings(settingsDir.filePath("typetool.ini"),
                     QSettings::IniFormat);
  settings.setValue("FontStyle", combo.currentData());
  settings.sync();
  check(settings.value("FontStyle").toString() == QStringLiteral("Bold"),
        "Text history persisted a translated style");

  QFontDatabase database;
  bool fontChecked = false;
  for (const QString &family : database.families()) {
    if (!database.styles(family).contains("Bold")) continue;
    const QFont before = database.font(family, "Bold", 24);
    const QFont after =
        database.font(family, combo.currentData().toString(), 24);
    check(before.toString() == after.toString() &&
              render(before) == render(after),
          "Font selection or rendered glyphs changed under translation");
    fontChecked = true;
    break;
  }
  check(fontChecked, "No installed Bold font available for render regression");

  const QString path = QString::fromUtf8("C:/Animación/%1/shot");
  check(DVGui::fileBrowserLocationName(DVGui::FileBrowserLocation::ProjectRoot,
                                       path)
            .endsWith(QStringLiteral("(") + path + ")"),
        "Translated project label modified the actual Unicode path");
  check(DVGui::fileBrowserLocationName(DVGui::FileBrowserLocation::Documents) ==
            QStringLiteral("Mis documentos"),
        "File Browser label was not translated");

  // Select Spanish while the OS/default Qt locale is Japanese.
  QLocale::setDefault(QLocale(QLocale::Japanese));
  check(
      DVGui::uiTranslationLocale(spanish, "Español") == QStringLiteral("es_ES"),
      "Application locale did not come from the selected catalog");
  QTranslator empty;
  check(DVGui::uiTranslationLocale(empty, "English") == QStringLiteral("en_US"),
        "English UI incorrectly inherited the OS locale");
  check(DVGui::uiTranslationLocale(empty, "Español") == QStringLiteral("es_ES"),
        "Missing application catalog lost the selected language");
  QTranslator qtSpanish;
  check(qtSpanish.load("qt_ui", catalogs.filePath("spanish")),
        "Packaged standard dialog catalog did not load");
  app.installTranslator(&qtSpanish);
  QMessageBox dialog(QMessageBox::Question, "Test", "Test",
                     QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
  check(dialog.button(QMessageBox::Yes)->text().remove('&') ==
            QString::fromUtf8("Sí"),
        "Standard Qt button did not follow the selected UI language");
  app.removeTranslator(&qtSpanish);
  // Exercise the optional full Qt loader without bundling third-party catalogs.
  QTemporaryDir qtDirectory;
  QFile::copy(catalogs.filePath("spanish/qt_ui.qm"),
              qtDirectory.filePath("qtbase_es.qm"));
  QTranslator fullQt;
  check(DVGui::loadQtUiTranslation(fullQt, "es_ES", {qtDirectory.path()}),
        "Optional Qt catalog did not use the selected language fallback");
  check(!DVGui::loadQtUiTranslation(fullQt, "ja_JP", {qtDirectory.path()}),
        "Qt loader used an available but unselected language");

  app.removeTranslator(&spanish);
  DVGui::populateFontStyleCombo(&combo, styles,
                                settings.value("FontStyle").toString());
  check(combo.currentText() == QStringLiteral("Bold") &&
            combo.currentData().toString() == QStringLiteral("Bold"),
        "Switching to English failed to restore the same original face");
  check(DVGui::fontStyleDisplayName("SemiBold") == QStringLiteral("SemiBold"),
        "Untranslated alias was unnecessarily renamed");
  DVGui::populateFontStyleCombo(&combo, {}, "Bold");
  check(combo.currentIndex() == -1,
        "Empty font-style list has a selected face");
  DVGui::populateFontStyleCombo(&combo, {"Regular"}, "MissingFace");
  check(combo.currentData().toString() == QStringLiteral("Regular"),
        "Unavailable face did not fall back to a valid original face");

  for (const QString &language :
       catalogs.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    QTranslator ui, tools;
    check(ui.load("toonz", catalogs.filePath(language)),
          "Application catalog did not load");
    check(tools.load("tnztools", catalogs.filePath(language)),
          "Tool catalog did not load");
    check(!ui.translate("XsheetGUI::NoteArea", "Toggle Xsheet/Timeline")
               .isEmpty(),
          "Timeline tooltip missing from the compiled catalog");
    check(!ui.translate("XsheetGUI::NoteArea", "Add New Memo").isEmpty(),
          "Memo tooltip missing from the compiled catalog");
    check(!ui.translate("MainWindow", "&Timeline").isEmpty(),
          "Timeline menu missing from the compiled catalog");
    check(!tools.translate("ToonzRasterBrushTool", "Draw Order:").isEmpty(),
          "Brush option missing from the compiled tool context");
  }
  return failures ? 1 : 0;
}
