#pragma once

#ifdef _WIN32

#include "sourceeditorbridge.h"
#include "svgsourcepropertiespanel.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace SvgSourceEditor {

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
        event->type() == QEvent::ApplicationActivate)
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

    for (QWidget *topLevel : application->topLevelWidgets()) {
      if (QMainWindow *window = qobject_cast<QMainWindow *>(topLevel))
        ensureShortcutHost(window);

      for (QMenu *menu : topLevel->findChildren<QMenu *>())
        ensureWindowsMenu(menu);

      if (QMenu *topLevelMenu = qobject_cast<QMenu *>(topLevel))
        ensureWindowsMenu(topLevelMenu);
    }
  }
};

inline void activateSourceEditorUi() {
  registerSvgSourcePropertiesPanelFactory();
  registerSourceEditorCommand();

  QAction *action =
      CommandManager::instance()->getAction(kSourceEditorCommandId, false);
  QApplication *application = qobject_cast<QApplication *>(qApp);
  if (!application || !action) return;

  SourceEditorBridge::instance();

  static SourceEditorUiActivator *activator = nullptr;
  if (!activator) {
    activator = new SourceEditorUiActivator(action, application);
    activator->start();
  }
}

}  // namespace SvgSourceEditor

inline void activateSvgSourceEditorUiAfterApplicationStartup() {
  QTimer::singleShot(0, qApp, []() { SvgSourceEditor::activateSourceEditorUi(); });
}

Q_COREAPP_STARTUP_FUNCTION(activateSvgSourceEditorUiAfterApplicationStartup)

#endif
