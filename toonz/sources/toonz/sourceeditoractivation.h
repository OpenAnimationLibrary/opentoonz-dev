#pragma once

#ifdef _WIN32

#include "sourceeditorpanel.h"

#include <QAction>
#include <QApplication>
#include <QChildEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace SvgSourceEditor {

inline QString sourceEditorDefaultStyleSheet() {
  // Keep these defaults less specific than the public object-name selectors.
  // A user can therefore override any of them from Preferences > Interface >
  // Additional Style Sheet with #SvgSourceEditorCode and
  // #SvgSourceEditorLineNumbers, even though this safety block is appended
  // after the currently selected application theme.
  return QStringLiteral(R"QSS(

/* SVG_SOURCE_EDITOR_DEFAULT_QSS
   Public Additional Style Sheet selectors:
     #SvgSourceEditorCode
     #SvgSourceEditorLineNumbers
     #SvgSourceEditorViewport
*/
QPlainTextEdit[svgSourceEditor="true"] {
  background-color: #ffffff;
  color: #202020;
  border: 1px solid #b8b8b8;
  border-radius: 2px;
  selection-background-color: rgba(79, 123, 199, 0.82);
  selection-color: #ffffff;
  padding: 1px 0 1px 1px;
}
QPlainTextEdit[svgSourceEditor="true"]:hover {
  background-color: #ffffff;
  color: #202020;
  border-color: #9c9c9c;
}
QPlainTextEdit[svgSourceEditor="true"]:focus {
  background-color: #ffffff;
  color: #202020;
  border-color: #4f7bc7;
}
QWidget[svgSourceEditorLineNumbers="true"] {
  background-color: #f1f1f1;
  color: #606060;
}
)QSS");
}

inline void ensureSourceEditorDefaultStyleSheet(QApplication *application) {
  if (!application) return;

  const QString marker = QStringLiteral("SVG_SOURCE_EDITOR_DEFAULT_QSS");
  QString currentStyle = application->styleSheet();
  if (currentStyle.contains(marker)) return;

  currentStyle += sourceEditorDefaultStyleSheet();
  application->setStyleSheet(currentStyle);
}

// Installs the already registered Source Editor QAction into the actual
// OpenToonz main window and every Windows menu. The first experimental pass
// created the command during QCoreApplication startup, but an application-wide
// QAction does not receive shortcuts until it belongs to a widget, and room
// menus may be created lazily long after startup. This activator follows the
// UI lifecycle and repairs both conditions whenever windows or menus appear.
class SourceEditorUiActivator final : public QObject {
  QAction *m_action;
  bool m_scanPending = false;

public:
  SourceEditorUiActivator(QAction *action, QObject *parent)
      : QObject(parent), m_action(action) {}

  void start() {
    QApplication *application = qobject_cast<QApplication *>(qApp);
    if (!application || !m_action) return;

    application->installEventFilter(this);
    m_action->setShortcutContext(Qt::ApplicationShortcut);
    if (m_action->shortcut().isEmpty())
      m_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+E")));

    ensureSourceEditorDefaultStyleSheet(application);
    scheduleScan();
    QTimer::singleShot(250, this, [this]() { scanUi(); });
    QTimer::singleShot(1500, this, [this]() { scanUi(); });
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (!m_action || !event)
      return QObject::eventFilter(watched, event);

    if (event->type() == QEvent::Show ||
        event->type() == QEvent::ShowToParent ||
        event->type() == QEvent::Polish) {
      if (QMainWindow *window = qobject_cast<QMainWindow *>(watched))
        ensureShortcutHost(window);
      if (QMenu *menu = qobject_cast<QMenu *>(watched))
        ensureWindowsMenu(menu);
    }

    if (event->type() == QEvent::ChildAdded ||
        event->type() == QEvent::ApplicationActivate ||
        event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::StyleChange)
      scheduleScan();

    return QObject::eventFilter(watched, event);
  }

private:
  static QString normalizedTitle(QString title) {
    title.remove(QLatin1Char('&'));
    return title.simplified().toCaseFolded();
  }

  static bool isWindowsMenu(const QMenu *menu) {
    if (!menu) return false;

    const QString title = normalizedTitle(menu->title());
    const QString plain = normalizedTitle(QStringLiteral("Windows"));
    const QString stacked = normalizedTitle(
        QCoreApplication::translate("StackedMenuBar", "Windows"));
    const QString mainWindow = normalizedTitle(
        QCoreApplication::translate("MainWindow", "Windows"));

    return title == plain || title == stacked || title == mainWindow;
  }

  void ensureShortcutHost(QMainWindow *window) {
    if (!window || window->actions().contains(m_action)) return;
    window->addAction(m_action);
  }

  void ensureWindowsMenu(QMenu *menu) {
    if (!isWindowsMenu(menu) || menu->actions().contains(m_action)) return;

    const QList<QAction *> actions = menu->actions();
    if (!actions.isEmpty() && !actions.last()->isSeparator())
      menu->addSeparator();
    menu->addAction(m_action);
  }

  void scheduleScan() {
    if (m_scanPending) return;
    m_scanPending = true;
    QTimer::singleShot(0, this, [this]() {
      m_scanPending = false;
      scanUi();
    });
  }

  void scanUi() {
    QApplication *application = qobject_cast<QApplication *>(qApp);
    if (!application || !m_action) return;

    // Changing the selected theme or Additional Style Sheet replaces the
    // application style sheet. Reinstall the low-specificity source-editor
    // defaults afterward; ID selectors in the user's additional sheet remain
    // authoritative.
    ensureSourceEditorDefaultStyleSheet(application);

    const QWidgetList topLevels = application->topLevelWidgets();
    for (QWidget *topLevel : topLevels) {
      if (QMainWindow *window = qobject_cast<QMainWindow *>(topLevel))
        ensureShortcutHost(window);

      const QList<QMenu *> menus = topLevel->findChildren<QMenu *>();
      for (QMenu *menu : menus) ensureWindowsMenu(menu);

      if (QMenu *topLevelMenu = qobject_cast<QMenu *>(topLevel))
        ensureWindowsMenu(topLevelMenu);
    }
  }
};

inline void activateSourceEditorUi() {
  registerSourceEditorCommand();

  QAction *action =
      CommandManager::instance()->getAction(kSourceEditorCommandId, false);
  QApplication *application = qobject_cast<QApplication *>(qApp);
  if (!application || !action) return;

  static SourceEditorUiActivator *activator = nullptr;
  if (!activator) {
    activator = new SourceEditorUiActivator(action, application);
    activator->start();
  }
}

}  // namespace SvgSourceEditor

inline void activateSvgSourceEditorUiAfterApplicationStartup() {
  QTimer::singleShot(0, qApp, []() {
    SvgSourceEditor::activateSourceEditorUi();
  });
}

Q_COREAPP_STARTUP_FUNCTION(activateSvgSourceEditorUiAfterApplicationStartup)

#endif  // _WIN32
