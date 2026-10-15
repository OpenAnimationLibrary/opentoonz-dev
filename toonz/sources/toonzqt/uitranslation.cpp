#include "toonzqt/uitranslation.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QSignalBlocker>
#include <QTranslator>

namespace {

const char *const fontStyles[] = {
    QT_TRANSLATE_NOOP("FontStyleNames", "Regular"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Normal"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Book"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Roman"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Thin"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Extra Light"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Light"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Medium"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Semibold"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Demi Bold"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Bold"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Extra Bold"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Black"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Heavy"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Italic"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Oblique"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Bold Italic"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Bold Oblique"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Light Italic"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Medium Italic"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Semibold Italic"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Extra Bold Italic"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Black Italic"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Condensed"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Condensed Bold"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Condensed Italic"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Condensed Bold Italic"),
    QT_TRANSLATE_NOOP("FontStyleNames", "Expanded")};

QString normalizedStyle(const QString &style) {
  return style.simplified().toCaseFolded();
}

const QHash<QString, const char *> &fontStyleLookup() {
  static const QHash<QString, const char *> lookup = [] {
    QHash<QString, const char *> result;
    for (const char *style : fontStyles)
      result.insert(normalizedStyle(QString::fromLatin1(style)), style);
    result.insert(QStringLiteral("semi bold"), "Semibold");
    result.insert(QStringLiteral("demibold"), "Demi Bold");
    result.insert(QStringLiteral("extralight"), "Extra Light");
    result.insert(QStringLiteral("ultra light"), "Extra Light");
    result.insert(QStringLiteral("extrabold"), "Extra Bold");
    result.insert(QStringLiteral("ultra bold"), "Extra Bold");
    result.insert(QStringLiteral("semi bold italic"), "Semibold Italic");
    return result;
  }();
  return lookup;
}

}  // namespace

QString DVGui::fontStyleDisplayName(const QString &style) {
  const char *source = fontStyleLookup().value(normalizedStyle(style), nullptr);
  if (!source) return style;
  QString label = QCoreApplication::translate("FontStyleNames", source);
  // Without a translation, keep even an alias exactly as the font supplied it.
  return label == QString::fromLatin1(source) ? style : label;
}

QStringList DVGui::fontStyleDisplayNames(const QStringList &styles) {
  QStringList labels;
  QHash<QString, int> counts;
  for (const QString &style : styles) {
    QString label = fontStyleDisplayName(style);
    labels.append(label);
    ++counts[label];
  }
  for (int i = 0; i < labels.size(); ++i) {
    if (counts.value(labels[i]) > 1)
      labels[i] = QStringLiteral("%1 (%2)").arg(labels[i], styles[i]);
  }
  return labels;
}

void DVGui::populateFontStyleCombo(QComboBox *combo, const QStringList &styles,
                                   const QString &currentStyle) {
  QSignalBlocker blocker(combo);
  const QStringList labels = fontStyleDisplayNames(styles);
  combo->clear();
  for (int i = 0; i < styles.size(); ++i) {
    combo->addItem(labels[i], styles[i]);
    combo->setItemData(i, styles[i], Qt::ToolTipRole);
  }
  int index = combo->findData(currentStyle);
  combo->setCurrentIndex(index >= 0 ? index : (styles.isEmpty() ? -1 : 0));
}

QString DVGui::fileBrowserLocationName(FileBrowserLocation location,
                                       const QString &detail) {
  switch (location) {
  case FileBrowserLocation::Computer:
    return QCoreApplication::translate("FileBrowserLocations", "My Computer");
  case FileBrowserLocation::Network:
    return QCoreApplication::translate("FileBrowserLocations", "Network");
  case FileBrowserLocation::Documents:
    return QCoreApplication::translate("FileBrowserLocations", "My Documents");
  case FileBrowserLocation::Desktop:
    return QCoreApplication::translate("FileBrowserLocations", "Desktop");
  case FileBrowserLocation::Library:
    return QCoreApplication::translate("FileBrowserLocations", "Library");
  case FileBrowserLocation::History:
    return QCoreApplication::translate("FileBrowserLocations", "History");
  case FileBrowserLocation::ProjectRoot:
    return QCoreApplication::translate("FileBrowserLocations",
                                       "Project root (%1)")
        .arg(detail);
  case FileBrowserLocation::SceneFolder:
    return QCoreApplication::translate("FileBrowserLocations", "Scene Folder");
  default:
    return QString();
  }
}

QString DVGui::uiTranslationLocale(const QTranslator &catalog,
                                   const QString &languageName) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  if (!catalog.language().isEmpty()) return catalog.language();
#endif
  // Also handle a missing toonz.qm, and Qt versions before language() existed.
  static const QHash<QString, QString> locales = {
      {QStringLiteral("\u65e5\u672c\u8a9e"), QStringLiteral("ja_JP")},
      {QStringLiteral("Italiano"), QStringLiteral("it_IT")},
      {QStringLiteral("Fran\u00e7ais"), QStringLiteral("fr_FR")},
      {QStringLiteral("Espa\u00f1ol"), QStringLiteral("es_ES")},
      {QStringLiteral("\u4e2d\u6587"), QStringLiteral("zh_CN")},
      {QStringLiteral("Deutsch"), QStringLiteral("de_DE")},
      {QStringLiteral("\u0420\u0443\u0441\u0441\u043a\u0438\u0439"),
       QStringLiteral("ru_RU")},
      {QStringLiteral("\ud55c\uad6d\uc5b4"), QStringLiteral("ko_KR")},
      {QStringLiteral("\u010ce\u0161tina"), QStringLiteral("cs_CZ")},
      {QStringLiteral("Portuguese"), QStringLiteral("pt_BR")}};
  return locales.value(languageName, QStringLiteral("en_US"));
}

bool DVGui::loadQtUiTranslation(QTranslator &translator, const QString &locale,
                                const QStringList &directories) {
  const QLocale selectedLocale(locale.isEmpty() ? QStringLiteral("en_US")
                                                : locale);
  for (const QString &directory : directories) {
    if (translator.load(selectedLocale, "qt", "_", directory) ||
        translator.load(selectedLocale, "qtbase", "_", directory))
      return true;
  }
  return false;
}
