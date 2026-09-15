#include "touchpresetcommandmanager.h"

#include "mainwindow.h"
#include "shortcutpopup.h"
#include "tapp.h"
#include "touchpresetmanager.h"

#include "toonz/toonzfolders.h"
#include "toonzqt/gutil.h"
#include "toonzqt/menubarcommand.h"

#include "tsystem.h"
#include "tfilepath.h"

#include <QAction>
#include <QSettings>

namespace {

const char kCommandPrefix[] = "MI_TouchWorkspacePreset_";

class TouchPresetActivateHandler final : public CommandHandlerInterface {
  QString m_presetName;

public:
  explicit TouchPresetActivateHandler(const QString &presetName)
      : m_presetName(presetName) {}

  void execute() override {
    TouchPresetManager::instance()->setActivePreset(m_presetName);
  }
};

void reloadShortcutsForCommands(const QSet<QString> &commandIds) {
  const TFilePath shortcutsFile =
      ToonzFolder::getMyModuleDir() + TFilePath("shortcuts.ini");
  if (!TFileStatus(shortcutsFile).doesExist()) return;

  QSettings settings(toQString(shortcutsFile), QSettings::IniFormat);
  settings.beginGroup(QStringLiteral("shortcuts"));
  for (const QString &commandId : commandIds) {
    const QString savedShortcut = settings.value(commandId, QString()).toString();
    if (savedShortcut.isEmpty()) continue;
    QAction *action =
        CommandManager::instance()->getAction(commandId.toStdString().c_str(),
                                                false);
    if (action) action->setShortcut(QKeySequence(savedShortcut));
  }
  settings.endGroup();
}

}  // namespace

//-----------------------------------------------------------------------------

TouchPresetCommandManager::TouchPresetCommandManager() {
  MainWindow *mainWindow =
      dynamic_cast<MainWindow *>(TApp::instance()->getMainWindow());
  if (mainWindow) {
    m_actionGroup = new QActionGroup(mainWindow);
    m_actionGroup->setExclusive(true);
  }
}

TouchPresetCommandManager *TouchPresetCommandManager::instance() {
  static TouchPresetCommandManager mgr;
  return &mgr;
}

QString TouchPresetCommandManager::commandIdForPreset(
    const QString &presetName) {
  return QString::fromLatin1(kCommandPrefix) + presetName;
}

QString TouchPresetCommandManager::presetNameFromCommandId(
    const QString &commandId) {
  const QString prefix = QString::fromLatin1(kCommandPrefix);
  if (!commandId.startsWith(prefix)) return QString();
  return commandId.mid(prefix.size());
}

void TouchPresetCommandManager::registerTouchPresetCommands() {
  MainWindow *mainWindow =
      dynamic_cast<MainWindow *>(TApp::instance()->getMainWindow());
  if (!mainWindow) return;

  if (!m_actionGroup) {
    m_actionGroup = new QActionGroup(mainWindow);
    m_actionGroup->setExclusive(true);
  }

  TouchPresetManager *mgr = TouchPresetManager::instance();
  const QStringList names = mgr->presetNames();
  const QString active    = mgr->activePreset();
  QSet<QString> current;

  for (const QString &name : names) {
    const QString commandId = commandIdForPreset(name);
    current.insert(commandId);

    QAction *existing = CommandManager::instance()->getAction(
        commandId.toStdString().c_str(), false);
    if (existing) {
      existing->setText(QObject::tr("[Touch] %1").arg(name));
      existing->setChecked(name == active);
      continue;
    }

    QAction *action =
        new DVAction(QObject::tr("[Touch] %1").arg(name), mainWindow);
    action->setCheckable(true);
    action->setChecked(name == active);
    action->setIcon(createQIcon(QStringLiteral("touch")));
    mainWindow->addAction(action);
    m_actionGroup->addAction(action);

    CommandManager::instance()->define(commandId.toStdString().c_str(),
                                       TouchPresetCommandType, "", action,
                                       "touch");
    CommandManager::instance()->setHandler(
        commandId.toStdString().c_str(),
        new TouchPresetActivateHandler(name));
  }

  const QSet<QString> obsolete = m_registeredIds - current;
  for (const QString &commandId : obsolete) {
    CommandManager::instance()->enable(commandId.toStdString().c_str(), false);
    QAction *action = CommandManager::instance()->getAction(
        commandId.toStdString().c_str(), false);
    if (action) action->setVisible(false);
  }

  m_registeredIds = current;
  reloadShortcutsForCommands(m_registeredIds);
  ShortcutPopup::refreshIfOpen();
}

void TouchPresetCommandManager::updateCheckedStates() {
  TouchPresetManager *mgr = TouchPresetManager::instance();
  const QString active    = mgr->activePreset();
  for (const QString &name : mgr->presetNames()) {
    const QString commandId = commandIdForPreset(name);
    QAction *action       = CommandManager::instance()->getAction(
        commandId.toStdString().c_str(), false);
    if (action) action->setChecked(name == active);
  }
}
