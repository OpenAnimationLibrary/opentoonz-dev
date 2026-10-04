#pragma once

#ifndef UI_TRANSLATION_INCLUDED
#define UI_TRANSLATION_INCLUDED

#include "tcommon.h"
#include <QStringList>

#undef DVAPI
#ifdef TOONZQT_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif

class QComboBox;
class QTranslator;

namespace DVGui {

// These are display labels only. Font selection and persistence use the
// original names returned by QFontDatabase.
QString DVAPI fontStyleDisplayName(const QString &style);
QStringList DVAPI fontStyleDisplayNames(const QStringList &styles);
void DVAPI populateFontStyleCombo(QComboBox *combo, const QStringList &styles,
                                  const QString &currentStyle);

enum class FileBrowserLocation {
  None,
  Computer,
  Network,
  Documents,
  Desktop,
  Library,
  History,
  ProjectRoot,
  SceneFolder
};

QString DVAPI fileBrowserLocationName(FileBrowserLocation location,
                                      const QString &detail = QString());

// The locale comes from the selected application catalog, never the OS locale.
QString DVAPI uiTranslationLocale(const QTranslator &catalog,
                                  const QString &languageName);
bool DVAPI loadQtUiTranslation(QTranslator &translator, const QString &locale,
                               const QStringList &directories);

}  // namespace DVGui

#endif
