#include "fullscreenworkspace.h"

#include "mainwindow.h"
#include "pane.h"
#include "sceneviewer.h"
#include "tapp.h"

#include "toonz/toonzfolders.h"

#include "shortcutpopup.h"

#include "toonzqt/dvdialog.h"
#include "toonzqt/imageutils.h"
#include "toonzqt/menubarcommand.h"

#include <QApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>
#include <QSettings>
#include <QTimer>

namespace {

TFilePath storePath() {
  return ToonzFolder::getMyModuleDir() +
         TFilePath("fullscreen_workspaces.ini");
}

QSettings openStore() {
  return QSettings(QString::fromStdWString(storePath().getWideString()),
                   QSettings::IniFormat);
}

bool isViewerPanelType(const std::string &panelType) {
  return panelType == "SceneViewer" || panelType == "ComboViewer";
}

QString panelTypeOf(const TPanel *panel) {
  return QString::fromStdString(panel->getPanelType());
}

class OpenWorkspaceHandler final : public CommandHandlerInterface {
public:
  explicit OpenWorkspaceHandler(const QString &workspaceName)
      : m_name(workspaceName) {}
  void execute() override { FullScreenWorkspace::instance().open(m_name); }

private:
  QString m_name;
};

class WorkspaceNameFilter final : public QObject {
public:
  explicit WorkspaceNameFilter(QMenu *menu) : QObject(menu) {}

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() != QEvent::MouseButtonPress) return false;
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::RightButton) return false;
    auto *menu = qobject_cast<QMenu *>(watched);
    if (!menu) return false;
    QAction *action = menu->actionAt(mouse->pos());
    if (!action || action->isSeparator() || !action->isEnabled()) return true;
    const QString name   = action->text();
    const QPoint global  = mouse->globalPos();
    QTimer::singleShot(0, menu, [name, global]() {
      QMenu ctx;
      QAction *rename = ctx.addAction(QObject::tr("Rename..."));
      QAction *remove = ctx.addAction(QObject::tr("Delete"));
      QAction *chosen = ctx.exec(global);
      if (chosen == rename)
        FullScreenWorkspace::instance().renameWorkspace(name);
      else if (chosen == remove)
        FullScreenWorkspace::instance().deleteWorkspace(name);
    });
    return true;
  }
};

QList<TPanel *> collectPanels(MainWindow *mw) {
  QList<TPanel *> panels;
  if (!mw) return panels;
  for (int i = 0; i < mw->getRoomCount(); ++i) {
    Room *room = mw->getRoom(i);
    if (!room) continue;
    panels.append(room->findChildren<TPanel *>());
  }
  return panels;
}

MainWindow *mainWindow() {
  return dynamic_cast<MainWindow *>(TApp::instance()->getMainWindow());
}

ImageUtils::FullScreenWidget *sceneViewerFullScreen() {
  ImageUtils::FullScreenWidget *visible = nullptr;
  ImageUtils::FullScreenWidget *any     = nullptr;
  const QWidgetList widgets             = QApplication::allWidgets();
  for (QWidget *widget : widgets) {
    auto *fs = qobject_cast<ImageUtils::FullScreenWidget *>(widget);
    if (!fs || !fs->widget() || !fs->widget()->inherits("SceneViewer"))
      continue;
    if (fs->windowState() & Qt::WindowFullScreen) return fs;
    if (!visible && fs->isVisible()) visible = fs;
    if (!any) any = fs;
  }

  if (SceneViewer *active = TApp::instance()->getActiveViewer()) {
    auto *fs = qobject_cast<ImageUtils::FullScreenWidget *>(
        active->parentWidget());
    if (fs) return fs;
  }
  return visible ? visible : any;
}

QString commandIdFor(const QString &name) {
  return QStringLiteral("MI_FullscreenWorkspace_") + name;
}

QString shortcutFilePath() {
  const TFilePath path =
      ToonzFolder::getMyModuleDir() + TFilePath("shortcuts.ini");
  return QString::fromStdWString(path.getWideString());
}

QRect targetScreen() {
  if (ImageUtils::FullScreenWidget *fs = sceneViewerFullScreen()) {
    if (QScreen *screen = fs->screen()) return screen->geometry();
  }
  if (QScreen *screen = QGuiApplication::primaryScreen())
    return screen->geometry();
  return QRect(0, 0, 1, 1);
}

QRectF toNorm(const QRect &geometry, const QRect &screen) {
  const double w = qMax(1, screen.width());
  const double h = qMax(1, screen.height());
  return QRectF((geometry.x() - screen.x()) / w,
                (geometry.y() - screen.y()) / h, geometry.width() / w,
                geometry.height() / h);
}

QRect fromNorm(const QRectF &norm, const QRect &screen) {
  return QRect(qRound(screen.x() + norm.x() * screen.width()),
               qRound(screen.y() + norm.y() * screen.height()),
               qMax(80, qRound(norm.width() * screen.width())),
               qMax(60, qRound(norm.height() * screen.height())));
}

QString encodeRect(const QRectF &norm) {
  return QStringLiteral("%1,%2,%3,%4")
      .arg(norm.x(), 0, 'f', 6)
      .arg(norm.y(), 0, 'f', 6)
      .arg(norm.width(), 0, 'f', 6)
      .arg(norm.height(), 0, 'f', 6);
}

QRectF decodeRect(const QString &text) {
  const QStringList parts = text.split(QLatin1Char(','));
  if (parts.size() != 4) return QRectF();
  return QRectF(parts.at(0).toDouble(), parts.at(1).toDouble(),
                parts.at(2).toDouble(), parts.at(3).toDouble());
}

QString cleanName(QString name) {
  name = name.trimmed();
  name.remove(QLatin1Char('/'));
  name.remove(QLatin1Char('\\'));
  name.remove(QLatin1Char('='));
  return name.trimmed();
}

void placePanel(TPanel *panel, const QRect &geometry) {
  panel->setSkipFloatingGeometrySave(true);
  QWidget *host = sceneViewerFullScreen();
  panel->setParent(host);
  panel->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                        Qt::WindowStaysOnTopHint);
  panel->setGeometry(geometry);
  panel->show();
  panel->raise();
}

}  // namespace

//-----------------------------------------------------------------------------

FullScreenWorkspace &FullScreenWorkspace::instance() {
  static FullScreenWorkspace workspace;
  return workspace;
}

//-----------------------------------------------------------------------------

QStringList FullScreenWorkspace::readNames() const {
  QSettings settings = openStore();
  return settings.value(QStringLiteral("catalog/names")).toStringList();
}

//-----------------------------------------------------------------------------

QVector<FullScreenWorkspace::Item> FullScreenWorkspace::readItems(
    const QString &name) const {
  QVector<Item> items;
  QSettings settings = openStore();
  settings.beginGroup(name);
  const QStringList types =
      settings.value(QStringLiteral("panels")).toStringList();
  for (const QString &type : types) {
    const QRectF norm = decodeRect(settings.value(type).toString());
    if (!norm.isValid() || norm.width() <= 0 || norm.height() <= 0) continue;
    items.append(Item{type, norm});
  }
  settings.endGroup();
  return items;
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::writeItems(const QString &name,
                                     const QVector<Item> &items) {
  QSettings settings = openStore();
  settings.remove(name);
  settings.beginGroup(name);
  QStringList types;
  for (const Item &item : items) {
    types.append(item.panelType);
    settings.setValue(item.panelType, encodeRect(item.norm));
  }
  settings.setValue(QStringLiteral("panels"), types);
  settings.endGroup();

  QStringList names = settings.value(QStringLiteral("catalog/names")).toStringList();
  if (!names.contains(name)) names.append(name);
  settings.setValue(QStringLiteral("catalog/names"), names);
  settings.sync();
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::removeStored(const QString &name) {
  QSettings settings = openStore();
  settings.remove(name);
  QStringList names = settings.value(QStringLiteral("catalog/names")).toStringList();
  names.removeAll(name);
  settings.setValue(QStringLiteral("catalog/names"), names);
  settings.sync();
}

//-----------------------------------------------------------------------------

QVector<FullScreenWorkspace::Item>
FullScreenWorkspace::captureVisiblePanels() const {
  QVector<Item> items;
  const QRect screen = targetScreen();
  QList<TPanel *> panels = collectPanels(mainWindow());
  for (const QPointer<TPanel> &held : m_placed) {
    if (held && !panels.contains(held.data())) panels.append(held.data());
  }
  for (TPanel *panel : panels) {
    if (!panel || !panel->isVisible()) continue;
    if (!panel->isFloating() && panel->parentWidget() != sceneViewerFullScreen())
      continue;
    const QString type = panelTypeOf(panel);
    if (type.isEmpty() || isViewerPanelType(panel->getPanelType())) continue;
    bool already = false;
    for (const Item &item : items) {
      if (item.panelType == type) {
        already = true;
        break;
      }
    }
    if (already) continue;
    const QRectF norm = toNorm(panel->frameGeometry(), screen);
    if (norm.width() <= 0 || norm.height() <= 0) continue;
    items.append(Item{type, norm});
  }
  return items;
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::placeItems(const QVector<Item> &items) {
  MainWindow *mw = mainWindow();
  if (!mw) return;
  const QRect screen = targetScreen();
  QList<TPanel *> panels = collectPanels(mw);
  for (const QPointer<TPanel> &held : m_placed) {
    if (held && !panels.contains(held.data())) panels.append(held.data());
  }

  for (const Item &item : items) {
    TPanel *panel = nullptr;
    for (TPanel *candidate : panels) {
      if (!candidate || panelTypeOf(candidate) != item.panelType ||
          !candidate->isFloating())
        continue;
      if (!panel || (candidate->isVisible() && !panel->isVisible()))
        panel = candidate;
    }

    bool created = false;
    if (!panel) {
      TMainWindow *room = TApp::instance()->getCurrentRoom();
      panel = TPanelFactory::createPanel(room, item.panelType);
      if (!panel) continue;
      created = true;
      panel->setSkipFloatingGeometrySave(true);
    }

    placePanel(panel, fromNorm(item.norm, screen));
    bool listed = false;
    for (const QPointer<TPanel> &held : m_placed) {
      if (held.data() == panel) {
        listed = true;
        break;
      }
    }
    if (!listed) m_placed.append(panel);
    for (FrozenPanel &frozen : m_frozen) {
      if (frozen.panel.data() == panel) frozen.touched = true;
    }
    if (created) {
      FrozenPanel frozen;
      frozen.panel   = panel;
      frozen.created = true;
      m_frozen.append(frozen);
    }
  }
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::concealForeignPanels(const QVector<Item> &items) {
  QList<TPanel *> panels = collectPanels(mainWindow());
  for (const QPointer<TPanel> &held : m_placed) {
    if (held && !panels.contains(held.data())) panels.append(held.data());
  }
  ImageUtils::FullScreenWidget *host = sceneViewerFullScreen();
  for (TPanel *panel : panels) {
    if (!panel || panel->isHidden()) continue;
    if (isViewerPanelType(panel->getPanelType())) continue;
    if (!panel->isFloating() && panel->parentWidget() != host) continue;

    const QString type = panelTypeOf(panel);
    bool saved         = false;
    for (const Item &item : items) {
      if (item.panelType == type) {
        saved = true;
        break;
      }
    }
    if (saved && panel->parentWidget() == host) continue;
    if (saved) {
      bool placed = false;
      for (const QPointer<TPanel> &held : m_placed) {
        if (held && panelTypeOf(held.data()) == type &&
            held->parentWidget() == host) {
          placed = true;
          break;
        }
      }
      if (!placed) continue;
    }

    for (FrozenPanel &frozen : m_frozen) {
      if (frozen.panel.data() != panel || frozen.created) continue;
      frozen.touched = true;
      break;
    }
    m_holding = true;
    panel->hide();
    m_holding = false;
  }
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::begin(QString name) {
  m_pendingName.clear();
  if (m_active || !m_frozen.isEmpty()) {
    QVector<QPointer<TPanel>> shown;
    const bool workspace = m_active;
    for (const FrozenPanel &frozen : m_frozen) {
      if (!frozen.panel || frozen.created) continue;
      const bool visible =
          workspace ? frozen.visible : !frozen.panel->isHidden();
      if (visible) shown.append(frozen.panel);
    }
    end();
    m_sessionVisible    = shown;
    m_hasSessionVisible = true;
  }

  const QVector<Item> items = readItems(name);

  MainWindow *mw = mainWindow();
  m_frozen.clear();
  m_placed.clear();
  for (TPanel *panel : collectPanels(mw)) {
    if (!panel) continue;
    FrozenPanel frozen;
    frozen.panel    = panel;
    frozen.floating = panel->isFloating();
    frozen.visible  = !panel->isHidden();
    frozen.flags        = panel->windowFlags();
    frozen.geometry     = panel->frameGeometry();
    frozen.parentWidget = panel->parentWidget();
    m_frozen.append(frozen);
  }
  if (m_hasSessionVisible) {
    for (FrozenPanel &frozen : m_frozen) {
      bool wasVisible = false;
      for (const QPointer<TPanel> &shown : m_sessionVisible) {
        if (shown.data() == frozen.panel.data()) {
          wasVisible = true;
          break;
        }
      }
      frozen.visible = wasVisible;
    }
    m_sessionVisible.clear();
    m_hasSessionVisible = false;
  }

  placeItems(items);
  concealForeignPanels(items);

  m_active     = true;
  m_activeName = name;
  raisePlaced();
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::end() {
  const bool workspace = m_active;
  const bool holding   = m_holding;
  m_holding            = true;
  m_active             = false;
  m_activeName.clear();
  m_placed.clear();

  for (const FrozenPanel &frozen : m_frozen) {
    TPanel *panel = frozen.panel;
    if (!panel) continue;
    if (frozen.created) {
      panel->setSkipFloatingGeometrySave(true);
      panel->hide();
      panel->setParent(nullptr);
      panel->deleteLater();
      continue;
    }
    if (!frozen.touched) continue;
    // A workspace puts back the session that existed before it opened.
    const bool show = workspace ? frozen.visible : !panel->isHidden();
    QWidget *parent = frozen.parentWidget;
    auto *room      = qobject_cast<TMainWindow *>(parent);
    if (room && !panel->parentLayout())
      room->addDockWidget(panel);
    else
      panel->setParent(parent);
    panel->setWindowFlags(frozen.flags);
    panel->setGeometry(frozen.geometry);
    if (show)
      panel->show();
    else
      panel->hide();
    panel->setSkipFloatingGeometrySave(false);
  }
  m_frozen.clear();
  m_holding = holding;
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::raisePlaced() {
  for (const QPointer<TPanel> &panel : m_placed) {
    if (!panel || panel->isHidden()) continue;
    panel->raise();
  }
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::holdVisibleFloatingPanels() {
  for (TPanel *panel : collectPanels(mainWindow())) {
    if (panel && panel->isVisible() && panel->isFloating()) holdPanel(panel);
  }
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::holdPanel(TPanel *panel) {
  if (!panel || m_holding) return;
  if (!ImageUtils::isViewerFullScreenActive()) return;
  if (!panel->isFloating() || isViewerPanelType(panel->getPanelType())) return;

  ImageUtils::FullScreenWidget *host = sceneViewerFullScreen();
  if (!host || !(host->windowState() & Qt::WindowFullScreen)) return;
  if (panel->parentWidget() == host) {
    if (!panel->isHidden()) panel->raise();
    return;
  }

  bool known = false;
  for (FrozenPanel &frozen : m_frozen) {
    if (frozen.panel.data() != panel) continue;
    frozen.touched = true;
    known          = true;
    break;
  }
  if (!known) {
    FrozenPanel frozen;
    frozen.panel        = panel;
    frozen.floating     = true;
    frozen.visible      = !m_active;
    frozen.touched      = true;
    frozen.flags        = panel->windowFlags();
    frozen.geometry     = panel->frameGeometry();
    frozen.parentWidget = panel->parentWidget();
    m_frozen.append(frozen);
  }

  QRect geometry = panel->frameGeometry();
  if (geometry.width() < 40 || geometry.height() < 40) {
    const QRect screen = targetScreen();
    geometry           = QRect(screen.x() + 48, screen.y() + 48, 420, 320);
  }

  m_holding = true;
  placePanel(panel, geometry);
  m_holding = false;

  for (const QPointer<TPanel> &held : m_placed) {
    if (held.data() == panel) return;
  }
  m_placed.append(panel);
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::onViewerFullScreenChanged(bool active) {
  if (!active) {
    if (!ImageUtils::isViewerFullScreenActive() &&
        (m_active || !m_frozen.isEmpty()))
      end();
    return;
  }
  if (!m_pendingName.isEmpty()) {
    const QString name = m_pendingName;
    begin(name);
    return;
  }
  holdVisibleFloatingPanels();
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::raiseCompanionPanels() { raisePlaced(); }

//-----------------------------------------------------------------------------

void FullScreenWorkspace::open(const QString &name) {
  ImageUtils::FullScreenWidget *fs = sceneViewerFullScreen();
  if (!fs) {
    DVGui::warning(
        QObject::tr("Open a Viewer before a fullscreen workspace."));
    return;
  }
  if ((fs->windowState() & Qt::WindowFullScreen) && m_active &&
      m_activeName == name) {
    const QVector<Item> items = readItems(name);
    placeItems(items);
    concealForeignPanels(items);
    raisePlaced();
    return;
  }
  m_pendingName = name;
  if (fs->windowState() & Qt::WindowFullScreen) {
    begin(name);
    return;
  }
  m_sessionVisible.clear();
  m_hasSessionVisible = true;
  for (TPanel *panel : collectPanels(mainWindow())) {
    if (panel && !panel->isHidden()) m_sessionVisible.append(panel);
  }
  if (!fs->toggleFullScreen(false)) {
    m_pendingName.clear();
    m_sessionVisible.clear();
    m_hasSessionVisible = false;
  }
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::save() {
  if (!m_active) {
    saveAs();
    return;
  }
  writeItems(m_activeName, captureVisiblePanels());
  registerCommands();
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::saveAs() {
  bool ok          = false;
  const QString in = DVGui::getText(QObject::tr("Save Fullscreen Workspace"),
                                    QObject::tr("Name:"), m_activeName, &ok);
  if (!ok) return;
  const QString name = cleanName(in);
  if (name.isEmpty() || name == QLatin1String("catalog")) return;

  writeItems(name, captureVisiblePanels());
  if (ImageUtils::isViewerFullScreenActive()) {
    m_active     = true;
    m_activeName = name;
  } else if (m_active) {
    m_activeName = name;
  }
  registerCommands();
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::renameWorkspace(const QString &current) {
  if (current.isEmpty() || !readNames().contains(current)) return;
  bool ok          = false;
  const QString in = DVGui::getText(QObject::tr("Rename Fullscreen Workspace"),
                                    QObject::tr("New name:"), current, &ok);
  if (!ok) return;
  const QString name = cleanName(in);
  if (name.isEmpty() || name == current || name == QLatin1String("catalog"))
    return;
  const QVector<Item> items = readItems(current);
  writeItems(name, items);
  removeStored(current);
  if (m_activeName == current) m_activeName = name;
  QSettings settings(shortcutFilePath(), QSettings::IniFormat);
  settings.beginGroup(QStringLiteral("shortcuts"));
  const QString saved = settings.value(commandIdFor(current)).toString();
  settings.remove(commandIdFor(current));
  if (!saved.isEmpty()) settings.setValue(commandIdFor(name), saved);
  settings.endGroup();
  registerCommands();
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::deleteWorkspace(const QString &name) {
  if (name.isEmpty() || !readNames().contains(name)) return;
  const int answer = DVGui::MsgBox(
      QObject::tr("Delete fullscreen workspace \"%1\"?").arg(name),
      QObject::tr("Delete"), QObject::tr("Cancel"), 1);
  if (answer != 1) return;
  if (m_active && m_activeName == name) end();
  removeStored(name);
  QSettings settings(shortcutFilePath(), QSettings::IniFormat);
  settings.beginGroup(QStringLiteral("shortcuts"));
  settings.remove(commandIdFor(name));
  settings.endGroup();
  registerCommands();
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::registerCommands() {
  MainWindow *mw = mainWindow();
  if (!mw) return;

  QStringList currentIds;
  for (const QString &name : readNames()) {
    if (!name.trimmed().isEmpty()) currentIds.append(commandIdFor(name));
  }

  for (const QString &oldId : m_registeredCommandIds) {
    if (currentIds.contains(oldId)) continue;
    QAction *oldAction = CommandManager::instance()->getAction(
        oldId.toStdString().c_str(), false);
    if (!oldAction) continue;
    oldAction->setShortcut(QKeySequence());
    oldAction->setEnabled(false);
    oldAction->setVisible(false);
    mw->removeAction(oldAction);
  }
  m_registeredCommandIds.clear();

  for (const QString &name : readNames()) {
    if (name.trimmed().isEmpty()) continue;
    const QString commandId = commandIdFor(name);
    const QString label     = QStringLiteral("[Fullscreen] ") + name;
    QAction *action         = CommandManager::instance()->getAction(
        commandId.toStdString().c_str(), false);
    if (!action) {
      action = new DVAction(label, mw);
      mw->addAction(action);
      CommandManager::instance()->define(commandId.toStdString().c_str(),
                                         FullScreenWorkspaceCommandType, "",
                                         action, "");
      CommandManager::instance()->setHandler(commandId.toStdString().c_str(),
                                             new OpenWorkspaceHandler(name));
    } else {
      action->setText(label);
      action->setVisible(true);
      action->setEnabled(true);
      if (!mw->actions().contains(action)) mw->addAction(action);
    }
    m_registeredCommandIds.append(commandId);
  }

  QSettings settings(shortcutFilePath(), QSettings::IniFormat);
  settings.beginGroup(QStringLiteral("shortcuts"));
  for (const QString &commandId : m_registeredCommandIds) {
    const QString saved = settings.value(commandId).toString();
    if (saved.isEmpty()) continue;
    QAction *action = CommandManager::instance()->getAction(
        commandId.toStdString().c_str(), false);
    if (!action) continue;
    const QString canonical = QKeySequence(saved).toString();
    if (!canonical.isEmpty())
      CommandManager::instance()->setShortcut(action,
                                             canonical.toStdString());
  }
  settings.endGroup();
  ShortcutPopup::refreshIfOpen();
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::addSaveActions(QMenu *menu) {
  if (!menu) return;
  menu->addSeparator();
  QAction *save = menu->addAction(QObject::tr("Save Fullscreen Workspace"));
  QObject::connect(save, &QAction::triggered, menu,
                   []() { instance().save(); });
  QAction *saveAs =
      menu->addAction(QObject::tr("Save Fullscreen Workspace As..."));
  QObject::connect(saveAs, &QAction::triggered, menu,
                   []() { instance().saveAs(); });

  QMenu *workspaces = menu->addMenu(QObject::tr("Fullscreen Workspaces"));
  workspaces->installEventFilter(new WorkspaceNameFilter(workspaces));
  QObject::connect(workspaces, &QMenu::aboutToShow, workspaces,
                   [workspaces]() { instance().fillMenu(workspaces); });
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::attachMenu(QMenu *workspaceMenu) {
  if (!workspaceMenu) return;
  for (QAction *action : workspaceMenu->actions()) {
    if (action->menu() &&
        action->menu()->objectName() == QStringLiteral("fullscreenWorkspaceMenu"))
      return;
  }
  QMenu *menu = workspaceMenu->addMenu(QObject::tr("Fullscreen"));
  menu->setObjectName(QStringLiteral("fullscreenWorkspaceMenu"));
  menu->installEventFilter(new WorkspaceNameFilter(menu));
  QObject::connect(menu, &QMenu::aboutToShow, menu,
                   [menu]() { instance().fillMenu(menu); });
}

//-----------------------------------------------------------------------------

void FullScreenWorkspace::fillMenu(QMenu *menu) {
  menu->clear();
  const QStringList names = readNames();
  if (names.isEmpty()) {
    QAction *empty = menu->addAction(QObject::tr("(No Workspace)"));
    empty->setEnabled(false);
  }
  for (const QString &name : names) {
    if (name.trimmed().isEmpty()) continue;
    QAction *action = menu->addAction(name);
    action->setCheckable(true);
    action->setChecked(m_active && m_activeName == name);
    QObject::connect(action, &QAction::triggered, menu, [name]() {
      instance().open(name);
    });
  }
}
