Warning: truncated output (original token count: 43285)
Total output lines: 4051



#include "mainwindow.h"
#include "customhelplink.h"

// Tnz6 includes
#include "menubar.h"
#include "menubarcommandids.h"
#include "xsheetviewer.h"
#include "viewerpane.h"
#include "flipbook.h"
#include "messagepanel.h"
#include "iocommand.h"
#include "tapp.h"
#include "comboviewerpane.h"
#include "startuppopup.h"
#include "tooloptionsshortcutinvoker.h"
#include "custompanelmanager.h"
#include "audiorecordingpopup.h"
#include "pltgizmopopup.h"
#include "shortcutpopup.h"
#ifdef ENABLE_CRASH_REPORTER_TEST
#include "crashhandler.h"
#endif

// TnzTools includes
#include "tools/toolcommandids.h"
#include "tools/toolhandle.h"

// TnzQt includes
#include "toonzqt/gutil.h"
#include "toonzqt/icongenerator.h"
#include "toonzqt/viewcommandids.h"
#include "toonzqt/updatechecker.h"
#include "toonzqt/paletteviewer.h"
#include "toonzqt/lutcalibrator.h"
#include "toonzqt/seethroughwindow.h"

// TnzLib includes
#include "toonz/toonzfolders.h"
#include "toonz/stage2.h"
#include "toonz/stylemanager.h"
#include "toonz/tscenehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/txshleveltypes.h"
#include "toonz/tproject.h"

// TnzBase includes
#include "tenv.h"

// TnzCore includes
#include "tsystem.h"
#include "timagecache.h"
#include "tthread.h"

// Qt includes
#include <QStackedWidget>
#include <QSettings>
#include <QApplication>
#include <QGLPixelBuffer>
#include <QDebug>
#include <QDesktopServices>
#include <QButtonGroup>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#ifdef _WIN32
#include <QtPlatformHeaders/QWindowsWindowFunctions>
#endif
#include <docklayout.h>

TEnv::IntVar ViewCameraToggleAction("ViewCameraToggleAction", 1);
TEnv::IntVar ViewTableToggleAction("ViewTableToggleAction", 1);
TEnv::IntVar FieldGuideToggleAction("FieldGuideToggleAction", 0);
TEnv::IntVar ViewBBoxToggleAction("ViewBBoxToggleAction1", 1);
TEnv::IntVar EditInPlaceToggleAction("EditInPlaceToggleAction", 0);
TEnv::IntVar RasterizePliToggleAction("RasterizePliToggleAction", 0);
TEnv::IntVar SafeAreaToggleAction("SafeAreaToggleAction", 0);
TEnv::IntVar ViewColorcardToggleAction("ViewColorcardToggleAction", 1);
TEnv::IntVar ViewGuideToggleAction("ViewGuideToggleAction", 1);
TEnv::IntVar ViewRulerToggleAction("ViewRulerToggleAction", 1);
TEnv::IntVar TCheckToggleAction("TCheckToggleAction", 0);
TEnv::IntVar ICheckToggleAction("ICheckToggleAction", 0);
TEnv::IntVar Ink1CheckToggleAction("Ink1CheckToggleAction", 0);
TEnv::IntVar PCheckToggleAction("PCheckToggleAction", 0);
TEnv::IntVar IOnlyToggleAction("IOnlyToggleAction", 0);
TEnv::IntVar BCheckToggleAction("BCheckToggleAction", 0);
TEnv::IntVar GCheckToggleAction("GCheckToggleAction", 0);
TEnv::IntVar ACheckToggleAction("ACheckToggleAction", 0);
TEnv::IntVar LinkToggleAction("LinkToggleAction", 0);
TEnv::IntVar DockingCheckToggleAction("DockingCheckToggleAction", 0);
TEnv::IntVar ShiftTraceToggleAction("ShiftTraceToggleAction", 0);
TEnv::IntVar EditShiftToggleAction("EditShiftToggleAction", 0);
TEnv::IntVar ShowShiftOriginToggleAction("ShowShiftOriginToggleAction", 0);
TEnv::IntVar NoShiftToggleAction("NoShiftToggleAction", 0);
TEnv::IntVar TouchGestureControl("TouchGestureControl", 0);
TEnv::IntVar ShowBuildDateInTitle("ShowBuildDateInTitle", 1);

//=============================================================================
namespace {
//=============================================================================

// layout file name may be overwritten by the argument
std::string layoutsFileName           = "layouts.txt";
const std::string currentRoomFileName = "currentRoom.txt";
bool scrambledRooms                   = false;

//=============================================================================

bool readRoomList(std::vector<TFilePath> &roomPaths,
                  const QString &argumentLayoutFileName) {
  bool argumentLayoutFileLoaded = false;

  TFilePath fp;
  /*-レイアウトファイルが指定されている場合--*/
  if (!argumentLayoutFileName.isEmpty()) {
    fp = ToonzFolder::getRoomsFile(argumentLayoutFileName.toStdString());
    if (!TFileStatus(fp).doesExist()) {
      DVGui::warning("Room layout file " + argumentLayoutFileName +
                     " not found!");
      fp = ToonzFolder::getRoomsFile(layoutsFileName);
      if (!TFileStatus(fp).doesExist()) return false;
    } else {
      argumentLayoutFileLoaded = true;
      layoutsFileName          = argumentLayoutFileName.toStdString();
    }
  } else {
    fp = ToonzFolder::getRoomsFile(layoutsFileName);
    if (!TFileStatus(fp).doesExist()) return false;
  }

  Tifstream is(fp);
  for (;;) {
    char buffer[1024];
    is.getline(buffer, sizeof(buffer));
    if (is.eof()) break;
    char *s = buffer;
    while (*s == ' ' || *s == '\t') s++;
    char *t = s;
    while (*t && *t != '\r' && *t != '\n') t++;
    while (t > s && (t[-1] == ' ' || t[-1] == '\t')) t--;
    t[0] = '\0';
    if (s[0] == '\0') continue;
    TFilePath roomPath = fp.getParentDir() + s;
    roomPaths.push_back(roomPath);
  }

  return argumentLayoutFileLoaded;
}

//-----------------------------------------------------------------------------

void writeRoomList(std::vector<TFilePath> &roomPaths) {
  TFilePath fp = ToonzFolder::getMyRoomsDir() + layoutsFileName;
  TSystem::touchParentDir(fp);
  Tofstream os(fp);
  if (!os) return;
  for (int i = 0; i < (int)roomPaths.size(); i++) {
    TFilePath roomPath = roomPaths[i];
    assert(roomPath.getParentDir() == fp.getParentDir());
    os << roomPath.withoutParentDir() << "\n";
  }
}

//-----------------------------------------------------------------------------

void writeRoomList(std::vector<Room *> &rooms) {
  std::vector<TFilePath> roomPaths;
  for (int i = 0; i < (int)rooms.size(); i++)
    roomPaths.push_back(rooms[i]->getPath());
  writeRoomList(roomPaths);
}

//-----------------------------------------------------------------------------

void makePrivate(Room *room) {
  TFilePath layoutDir       = ToonzFolder::getMyRoomsDir();
  TFilePath roomPath        = room->getPath();
  std::string mbSrcFileName = roomPath.getName() + "_menubar.xml";
  if (roomPath == TFilePath() || roomPath.getParentDir() != layoutDir) {
    int count = 1;
    for (;;) {
      roomPath = layoutDir + ("room" + std::to_string(count++) + ".ini");
      if (!TFileStatus(roomPath).doesExist()) break;
    }
    room->setPath(roomPath);
    TSystem::touchParentDir(roomPath);
    room->save();
  }
  /*- create private menubar settings if not exists -*/
  std::string mbDstFileName = roomPath.getName() + "_menubar.xml";
  TFilePath myMBPath        = layoutDir + mbDstFileName;
  if (!TFileStatus(myMBPath).isReadable()) {
    TFilePath templateRoomMBPath =
        ToonzFolder::getTemplateRoomsDir() + mbSrcFileName;
    if (TFileStatus(templateRoomMBPath).doesExist())
      TSystem::copyFile(myMBPath, templateRoomMBPath);
    else {
      TFilePath templateFullMBPath =
          ToonzFolder::getTemplateRoomsDir() + "menubar_template.xml";
      if (TFileStatus(templateFullMBPath).doesExist())
        TSystem::copyFile(myMBPath, templateFullMBPath);
      else
        DVGui::warning(
            QObject::tr("Cannot open menubar settings template file. "
                        "Re-installing Toonz will solve this problem."));
    }
  }
}

//-----------------------------------------------------------------------------

void makePrivate(std::vector<Room *> &rooms) {
  for (int i = 0; i < (int)rooms.size(); i++) makePrivate(rooms[i]);
}

// major version  :  7 bits
// minor version  :  8 bits
// revision number: 16 bits
int get_version_code_from(std::string ver) {
  int version = 0;

  // major version: assume that the major version is less than 127.
  std::string::size_type const a = ver.find('.');
  std::string const major = (a == std::string::npos) ? ver : ver.substr(0, a);
  version += std::stoi(major) << 24;
  if ((a == std::string::npos) || (a + 1 == ver.length())) {
    return version;
  }

  // minor version: assume that the minor version is less than 255.
  std::string::size_type const b = ver.find('.', a + 1);
  std::string const minor        = (b == std::string::npos)
                                       ? ver.substr(a + 1)
                                       : ver.substr(a + 1, b - a - 1);
  version += std::stoi(minor) << 16;
  if ((b == std::string::npos) || (b + 1 == ver.length())) {
    return version;
  }

  // revision number: assume that the revision number is less than 32767.
  version += std::stoi(ver.substr(b + 1));

  return version;
}

}  // namespace
//=============================================================================

//=============================================================================
// Room
//-----------------------------------------------------------------------------
void copyQSettings(QSettings &source, QSettings &destination,
                   bool commit = true) {
  // 1. Get all keys in the current group
  QStringList keys = source.allKeys();

  // 2. Iterate and copy all simple key-value pairs
  for (const QString &key : keys) {
    destination.setValue(key, source.value(key));
  }

  // 3. Get all sub-groups
  QStringList groups = source.childGroups();

  // 4. Recursively process all groups (sections)
  for (const QString &group : groups) {
    // Move into the source group
    source.beginGroup(group);
    // Move into the destination group
    destination.beginGroup(group);

    // Recursively call the copy function for the subgroup
    copyQSettings(source, destination, false);  // Don't sync yet

    // Go back up for both
    destination.endGroup();
    source.endGroup();
  }

  // 5. Save the changes if commit is requested
  if (commit) {
    destination.sync();
  }
}

void Room::save() {
  if (!m_initialized && m_settings) {
    QSettings *newSettings =
        new QSettings(getPath().getQString(), QSettings::Format::IniFormat);
    copyQSettings(*m_settings, *newSettings, true);
    m_settings.reset(newSettings);
    return;
  }
  DockLayout *layout = dockLayout();

  // Now save layout state
  DockLayout::State state        = layout->saveState();
  std::vector<QRect> &geometries = state.first;

  QSettings settings(toQString(getPath()), QSettings::IniFormat);
  settings.remove("");

  settings.beginGroup("room");

  int i;
  for (i = 0; i < layout->count(); ++i) {
    settings.beginGroup("pane_" + QString::number(i));
    TPanel *pane = static_cast<TPanel *>(layout->itemAt(i)->widget());
    settings.setValue("name", pane->objectName());
    settings.setValue("geometry", geometries[i]);  // Use passed geometry
    // Room binding state persistence (custom panel feature)
    settings.setValue("roomBound", pane->isRoomBound());
    settings.setValue("boundRoomName", pane->getBoundRoomName());
    if (SaveLoadQSettings *persistent =
            dynamic_cast<SaveLoadQSettings *>(pane->widget()))
      persistent->save(settings);
    if (pane->getViewType() != -1)
      // If panel has different viewtypes, store current one
      settings.setValue("viewtype", pane->getViewType());
    if (pane->objectName() == "FlipBook") {
      // Store flipbook's identification number
      FlipBook *flip = static_cast<FlipBook *>(pane->widget());
      settings.setValue("index", flip->getPoolIndex());
    }
    settings.endGroup();
  }

  // Adding hierarchy string
  settings.setValue("hierarchy", state.second);
  settings.setValue("name", QVariant(QString(m_name)));

  settings.endGroup();
}

//-----------------------------------------------------------------------------
void Room::load(const TFilePath &fp, RoomLoadParams &params) {
  if (!m_initialized || !m_settings) {
    m_settings.reset(new QSettings(toQString(fp), QSettings::IniFormat));
  }

  setPath(fp);
  DockLayout *layout = dockLayout();

  m_settings->beginGroup("room");
  QStringList itemsList = m_settings->childGroups();

  QString roomName = m_settings->value("name").toString();
  setName(roomName);

  if (params.activeRoomName.isEmpty()) params.activeRoomName = roomName;

  if (!params.forceBuildGui && params.activeRoomName != roomName) {
    m_settings->endGroup();
    return;
  }

  std::vector<QRect> geometries;
  unsigned int i;
  for (i = 0; i < itemsList.size(); i++) {
    // Panel i
    // NOTE: Panels have to be retrieved in the precise order they were saved.
    // settings.beginGroup(itemsList[i]);  //NO! itemsList has lexicographical
    // ordering!!
    m_settings->beginGroup("pane_" + QString::number(i));

    TPanel *pane = 0;
    QString paneObjectName;

    // Retrieve panel name
    QVariant name = m_settings->value("name");
    if (name.canConvert(QVariant::String)) {
      // Allocate panel
      paneObjectName          = name.toString();
      std::string paneStrName = paneObjectName.toStdString();
      pane = TPanelFactory::createPanel(this, paneObjectName);
      if (SaveLoadQSettings *persistent =
              dynamic_cast<SaveLoadQSettings *>(pane->widget()))
        persistent->load(*m_settings);
    }

    if (!pane) {
      // Allocate a message panel
      MessagePanel *message = new MessagePanel(this);
      message->setWindowTitle(name.toString());
      message->setMessage(
          "This panel is not supported by the currently set license!");

      pane = message;
      pane->setPanelType(paneObjectName.toStdString());
      pane->setObjectName(paneObjectName);
    }

    pane->setObjectName(paneObjectName);

    // Restore room binding state (custom panel feature)
    pane->setRoomBound(m_settings->value("roomBound", false).toBool());
    pane->setBoundRoomName(m_settings->value("boundRoomName", "").toString());

    // Add panel to room
    addDockWidget(pane);

    // Store its geometry
    geometries.push_back(m_settings->value("geometry").toRect());

    // Restore view type if present
    if (m_settings->contains("viewtype"))
      pane->setViewType(m_settings->value("viewtype").toInt());

    // Restore flipbook pool indices
    if (paneObjectName == "FlipBook") {
      int index = m_settings->value("index").toInt();
      dynamic_cast<FlipBook *>(pane->widget())->setPoolIndex(index);
    }

    m_settings->endGroup();
  }

  // resolve resize events here to avoid unwanted minimize of floating viewer
  qApp->processEvents();

  DockLayout::State state(geometries,
                          m_settings->value("hierarchy").toString());

  layout->restoreState(state);

  // Store layout state for deferred re-apply after the first show.
  // Without this, TMainWindow::resizeEvent → redistribute() recalculates
  // panel sizes before the window has reached its final geometry.
  m_pendingLayoutState        = state;
  m_hasPendingLayoutRestore   = true;

  m_initialized = true;
}

//-----------------------------------------------------------------------------

void Room::showEvent(QShowEvent *event) {
  TMainWindow::showEvent(event);

  if (m_hasPendingLayoutRestore) {
    m_hasPendingLayoutRestore = false;
    DockLayout::State savedState = m_pendingLayoutState;
    DockLayout *layout           = dockLayout();
    QTimer::singleShot(0, this, [layout, savedState]() {
      layout->restoreState(savedState);
    });
  }
}

//=============================================================================
// MainWindow
//-----------------------------------------------------------------------------

MainWindow::MainWindow(const QString &argumentLayoutFileName, QWidget *parent,
                       Qt::WindowFlags flags)
    : QMainWindow(parent, flags)
    , m_saveSettingsOnQuit(true)
    , m_oldRoomIndex(0)
    , m_layoutName("") {
  // store a main window pointer in advance of making its contents
  TApp::instance()->setMainWindow(this);

  m_toolsActionGroup = new QActionGroup(this);
  m_toolsActionGroup->setExclusive(true);
  m_currentRoomsChoice = Preferences::instance()->getCurrentRoomChoice();
  defineActions();
  // user defined shortcuts will be loaded here
  CommandManager::instance()->loadShortcuts();

  // initialize tool options shortcuts
  ToolOptionsShortcutInvoker::instance()->initialize();

  TApp::instance()->getCurrentScene()->setDirtyFlag(false);

  // La menuBar altro non è che una toolbar
  // in cui posso inserire quanti custom widget voglio.
  m_topBar = new TopBar(this);

  addToolBar(m_topBar);
  addToolBarBreak(Qt::TopToolBarArea);

  m_stackedWidget = new QStackedWidget(this);

  // For the style sheet
  m_stackedWidget->setObjectName("MainStackedWidget");
  m_stackedWidget->setFrameStyle(QFrame::StyledPanel);

  // To give a border to the stackedWidget.
  /*QFrame *centralWidget = new QFrame(this);
centralWidget->setFrameStyle(QFrame::StyledPanel);
centralWidget->setObjectName("centralWidget");
QHBoxLayout *centralWidgetLayout = new QHBoxLayout;
centralWidgetLayout->setContentsMargins(3, 3, 3, 3);
centralWidgetLayout->addWidget(m_stackedWidget);
centralWidget->setLayout(centralWidgetLayout);*/

  setCentralWidget(m_stackedWidget);

  // Leggo i settings
  readSettings(argumentLayoutFileName);

  // Setto le stanze
  QTabBar *roomTabWidget = m_topBar->getRoomTabWidget();
  connect(m_stackedWidget, SIGNAL(currentChanged(int)),
          SLOT(onCurrentRoomChanged(int)));

  QObject::connect(roomTabWidget, &QTabBar::currentChanged, [this](int index) {
    Room *dstRoom = getRoom(index);
    if (dstRoom->notInitialized()) dstRoom->initialize();
    this->m_stackedWidget->setCurrentIndex(index);
  });

  /*-- タイトルバーにScene名を表示する --*/
  connect(TApp::instance()->getCurrentScene(), SIGNAL(nameSceneChanged()), this,
          SLOT(changeWindowTitle()));
  changeWindowTitle();

  // Connetto i comandi che sono in RoomTabWidget
  connect(roomTabWidget, SIGNAL(indexSwapped(int, int)),
          SLOT(onIndexSwapped(int, int)));
  connect(roomTabWidget, SIGNAL(insertNewTabRoom()), SLOT(insertNewRoom()));
  connect(roomTabWidget, SIGNAL(deleteTabRoom(int)), SLOT(deleteRoom(int)));
  connect(roomTabWidget, SIGNAL(renameTabRoom(int, const QString)),
          SLOT(renameRoom(int, const QString)));

  setCommandHandler("MI_Quit", this, &MainWindow::onQuit);
  setCommandHandler("MI_Undo", this, &MainWindow::onUndo);
  setCommandHandler("MI_Redo", this, &MainWindow::onRedo);
  setCommandHandler("MI_NewScene", this, &MainWindow::onNewScene);
  setCommandHandler("MI_LoadScene", this, &MainWindow::onLoadScene);
  setCommandHandler("MI_LoadSubSceneFile", this, &MainWindow::onLoadSubScene);
  setCommandHandler("MI_ResetRoomLayout", this, &MainWindow::resetRoomsLayout);
  setCommandHandler(MI_AutoFillToggle, this, &MainWindow::autofillToggle);

  setCommandHandler(MI_About, this, &MainWindow::onAbout);
  setCommandHandler(MI_OpenOnlineManual, this, &MainWindow::onOpenOnlineManual);
  setCommandHandler(MI_OpenWhatsNew, this, &MainWindow::onOpenWhatsNew);
  setCommandHandler(MI_Quicklink, this, &MainWindow::onOpenQuicklink);
  setCommandHandler(MI_OpenCommunityForum, this,
                    &MainWindow::onOpenCommunityForum);
  setCommandHandler(MI_OpenReportABug, this, &MainWindow::onOpenReportABug);
#ifdef ENABLE_CRASH_REPORTER_TEST
  setCommandHandler(MI_TestCrashReporter, this,
                    &MainWindow::onTestCrashReporter);
#endif

  setCommandHandler(MI_MaximizePanel, this, &MainWindow::maximizePanel);
  setCommandHandler(MI_FullScreenWindow, this, &MainWindow::fullScreenWindow);
  setCommandHandler(MI_SeeThroughWindow, this, &MainWindow::seeThroughWindow);
  setCommandHandler("MI_NewVectorLevel", this,
                    &MainWindow::onNewVectorLevelButtonPressed);
  setCommandHandler("MI_NewToonzRasterLevel", this,
                    &MainWindow::onNewToonzRasterLevelButtonPressed);
  setCommandHandler("MI_NewRasterLevel", this,
                    &MainWindow::onNewRasterLevelButtonPressed);
  setCommandHandler(MI_ClearCacheFolder, this, &MainWindow::clearCacheFolder);
  setCommandHandler("MI_NewMetaLevel", this,
                    &MainWindow::onNewMetaLevelButtonPressed);
  // remove ffmpegCache if still exists from crashed exit
  QString ffmpegCachePath =
      ToonzFolder::getCacheRootFolder().getQString() + "//ffmpeg";
  if (TSystem::doesExistFileOrLevel(TFilePath(ffmpegCachePath))) {
    TSystem::rmDirTree(TFilePath(ffmpegCachePath));
  }

  connect(TApp::instance(), SIGNAL(activeViewerChanged()), this,
          SLOT(onActiveViewerChanged()));
}

//-----------------------------------------------------------------------------

MainWindow::~MainWindow() {
  TEnv::saveAllEnvVariables();
  // cleanup ffmpeg cache
  QString ffmpegCachePath =
      ToonzFolder::getCacheRootFolder().getQString() + "//ffmpeg";
  if (TSystem::doesExistFileOrLevel(TFilePath(ffmpegCachePath))) {
    TSystem::rmDirTree(TFilePath(ffmpegCachePath));
  }
}

//-----------------------------------------------------------------------------

void MainWindow::changeWindowTitle() {
  TApp *app         = TApp::instance();
  ToonzScene *scene = app->getCurrentScene()->getScene();
  if (!scene) return;

  auto project        = scene->getProject();
  QString projectName = QString::fromStdString(project->getName().getName());

  QString sceneName = QString::fromStdWString(scene->getSceneName());

  if (sceneName.isEmpty()) sceneName = tr("Untitled");
  if (app->getCurrentScene()->getDirtyFlag()) sceneName += QString("*");

  /*--- レイアウトファイル名を頭に表示させる ---*/
  if (!m_layoutName.isEmpty()) sceneName.prepend(m_layoutName + " : ");

  QString name = sceneName + " [" + projectName + "] : " +
                 QString::fromStdString(TEnv::getApplicationFullName());

  if (ShowBuildDateInTitle) {
    name += " (built " __DATE__ " " __TIME__ ")";
  }
  setWindowTitle(name);
}

//-----------------------------------------------------------------------------

void MainWindow::changeWindowTitle(QString &str) { setWindowTitle(str); }

//-----------------------------------------------------------------------------

void MainWindow::startupFloatingPanels() {
  // Show all floating panels
  DockLayout *currDockLayout = getCurrentRoom()->dockLayout();
  int i;
  for (i = 0; i < currDockLayout->count(); ++i) {
    TPanel *currPanel = static_cast<TPanel *>(currDockLayout->widgetAt(i));
    if (currPanel->isFloating()) currPanel->show();
  }
}

//-----------------------------------------------------------------------------

Room *MainWindow::getRoom(int index) const {
  assert(index >= 0 && index < getRoomCount());
  return dynamic_cast<Room *>(m_stackedWidget->widget(index));
}

//-----------------------------------------------------------------------------
/*! Roomを名前から探す
 */
Room *MainWindow::getRoomByName(QString &roomName) {
  for (int i = 0; i < getRoomCount(); i++) {
    Room *room = dynamic_cast<Room *>(m_stackedWidget->widget(i));
    if (room) {
      if (room->getName() == roomName) return room;
    }
  }
  return 0;
}

//-----------------------------------------------------------------------------

int MainWindow::getRoomCount() const { return m_stackedWidget->count(); }

//-----------------------------------------------------------------------------

void MainWindow::refreshWriteSettings() { writeSettings(); }

//-----------------------------------------------------------------------------

void MainWindow::readSettings(const QString &argumentLayoutFileName) {
  QTabBar *roomTabWidget = m_topBar->getRoomTabWidget();

  /*-- Pageを追加すると同時にMenubarを追加する --*/
  StackedMenuBar *stackedMenuBar = m_topBar->getStackedMenuBar();

  std::vector<Room *> rooms;

  // leggo l'elenco dei layout
  std::vector<TFilePath> roomPaths;

  if (readRoomList(roomPaths, argumentLayoutFileName)) {
    if (!argumentLayoutFileName.isEmpty()) {
      /*--
       * タイトルバーに表示するレイアウト名を作る：_layoutがあればそこから省く。無ければ.txtのみ省く
       * --*/
      int pos      = (argumentLayoutFileName.indexOf("_layout") == -1)
                         ? argumentLayoutFileName.indexOf(".txt")
                         : argumentLayoutFileName.indexOf("_layout");
      m_layoutName = argumentLayoutFileName.left(pos);
    }
  }

  // Get Current Room
  TFilePath fp = ToonzFolder::getRoomsFile(currentRoomFileName);
  Tifstream is(fp);
  std::string name;
  is >> name;

  QString currentRoomName = QString::fromUtf8(name.c_str());
  Room::RoomLoadParams params;
  params.activeRoomName = currentRoomName;
  params.forceBuildGui  = !Preferences::instance()->isLazyLoadRoomsEnabled();

  // === CRITICAL FIX: Register dynamic commands BEFORE loading rooms ===
  // Custom Panels in rooms will try to link to these commands during load.
  // If commands don't exist yet, buttons become "empty shells".
  CustomPanelManager::instance()->loadCustomPanelEntries();

  for (int i = 0; i < (int)roomPaths.size(); i++) {
    TFilePath roomPath = roomPaths[i];
    if (TFileStatus(roomPath).doesExist()) {
      Room *room = new Room(this);
      room->load(roomPath, params);
      m_stackedWidget->addWidget(room);
      roomTabWidget->addTab(room->getName());

      /*- ここでMenuBarファイルをロードする -*/
      std::string mbFileName = roomPath.getName() + "_menubar.xml";
      stackedMenuBar->loadAndAddMenubar(ToonzFolder::getRoomsFile(mbFileName));

      // room->setDockOptions(QMainWindow::DockOptions(
      //  (QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks) &
      //  ~QMainWindow::AllowTabbedDocks));
      rooms.push_back(room);
    }
  }

  // Read the flipbook history
  FlipBookPool::instance()->load(ToonzFolder::getMyModuleDir() +
                                 TFilePath("fliphistory.ini"));

  /*- レイアウト設定ファイルが見つからなかった場合、初期Roomの生成 -*/
  // Se leggendo i settings non ho inizializzato le stanze lo faccio ora.
  // Puo' accadere se si buttano i file di inizializzazione.
  if (rooms.empty()) {
    // CleanupRoom
    Room *cleanupRoom = createCleanupRoom();
    m_stackedWidget->addWidget(cleanupRoom);
    rooms.push_back(cleanupRoom);
    stackedMenuBar->createMenuBarByName(cleanupRoom->getName());

    // PltEditRoom
    Room *pltEditRoom = createPltEditRoom();
    m_stackedWidget->addWidget(pltEditRoom);
    rooms.push_back(pltEditRoom);
    stackedMenuBar->createMenuBarByName(pltEditRoom->getName());

    // InknPaintRoom
    Room *inknPaintRoom = createInknPaintRoom();
    m_stackedWidget->addWidget(inknPaintRoom);
    rooms.push_back(inknPaintRoom);
    stackedMenuBar->createMenuBarByName(inknPaintRoom->getName());

    // XsheetRoom
    Room *xsheetRoom = createXsheetRoom();
    m_stackedWidget->addWidget(xsheetRoom);
    rooms.push_back(xsheetRoom);
    stackedMenuBar->createMenuBarByName(xsheetRoom->getName());

    // BatchesRoom
    Room *batchesRoom = createBatchesRoom();
    m_stackedWidget->addWidget(batchesRoom);
    rooms.push_back(batchesRoom);
    stackedMenuBar->createMenuBarByName(batchesRoom->getName());

    // BrowserRoom
    Room *browserRoom = createBrowserRoom();
    m_stackedWidget->addWidget(browserRoom);
    rooms.push_back(browserRoom);
    stackedMenuBar->createMenuBarByName(browserRoom->getName());
  }

  /*- If the layout files were loaded from template, then save them as private
   * ones -*/
  makePrivate(rooms);
  writeRoomList(rooms);

  // Set Current Room
  if (currentRoomName != "") {
    int count = m_stackedWidget->count();
    int index;
    for (index = 0; index < count; index++)
      if (getRoom(index)->getName() == currentRoomName) break;
    if (index < count) {
      m_oldRoomIndex = index;
      roomTabWidget->setCurrentIndex(index);
      m_stackedWidget->setCurrentIndex(index);
    }
  }

  // Register room shortcuts after all rooms are loaded
  // This allows users to assign keyboard shortcuts to switch between rooms
  registerRoomCommands();

  RecentFiles::instance()->loadRecentFiles();
  // Note: loadCustomPanelEntries() now called BEFORE loading rooms
  // to ensure dynamic commands exist when Custom Panels initialize
}

//-----------------------------------------------------------------------------

void MainWindow::writeSettings() {
  std::vector<Room *> rooms;
  int i;

  // Flipbook history
  DockLayout *currRoomLayout(getCurrentRoom()->dockLayout());
  for (i = 0; i < currRoomLayout->count(); ++i) {
    // Remove all floating flipbooks from current room and return them into
    // the flipbook pool.
    TPanel *panel = static_cast<TPanel *>(currRoomLayout->itemAt(i)->widget());
    if (panel->isFloating() && panel->getPanelType() == "FlipBook") {
      currRoomLayout->removeWidget(panel);
      FlipBook *flipbook = static_cast<FlipBook *>(panel->widget());
      FlipBookPool::instance()->push(flipbook);
      --i;
    }
  }

  FlipBookPool::instance()->save();

  // Room layouts
  for (i = 0; i < m_stackedWidget->count(); i++) {
    Room *room = getRoom(i);
    rooms.push_back(room);
    room->save();
  }
  if (m_currentRoomsChoice == Preferences::instance()->getCurrentRoomChoice()) {
    writeRoomList(rooms);
  }

  // Current room settings
  Tofstream os(ToonzFolder::getMyRoomsDir() + currentRoomFileName);
  os << getCurrentRoom()->getName().toStdString();

  // Main window settings
  TFilePath fp = ToonzFolder::getMyModuleDir() + TFilePath("mainwindow.ini");
  QSettings settings(toQString(fp), QSettings::IniFormat);

  settings.setValue("MainWindowGeometry", saveGeometry());
}

//-----------------------------------------------------------------------------

Room *MainWindow::createCleanupRoom() {
  Room *cleanupRoom = new Room(this);
  cleanupRoom->setName("Cleanup");
  cleanupRoom->setObjectName("CleanupRoom");

  m_topBar->getRoomTabWidget()->addTab(tr("Cleanup"));

  DockLayout *layout = cleanupRoom->dockLayout();

  // Viewer
  TPanel *viewer = TPanelFactory::createPanel(cleanupRoom, "ComboViewer");
  if (viewer) {
    cleanupRoom->addDockWidget(viewer);
    layout->dockItem(viewer);
    ComboViewerPanel *cvp = qobject_cast<ComboViewerPanel *>(viewer);
    if (cvp)
      // hide all parts
      cvp->setVisiblePartsFlag(VPPARTS_None);
  }

  // CleanupSettings
  TPanel *cleanupSettingsPane =
      TPanelFactory::createPanel(cleanupRoom, "CleanupSettings");
  if (cleanupSettingsPane) {
    cleanupRoom->addDockWidget(cleanupSettingsPane);
    layout->dockItem(cleanupSettingsPane, viewer, Region::right);
  }

  // Xsheet
  TPanel *xsheetPane = TPanelFactory::createPanel(cleanupRoom, "Xsheet");
  if (xsheetPane) {
    cleanupRoom->addDockWidget(xsheetPane);
    layout->dockItem(xsheetPane, viewer, Region::bottom);
  }

  return cleanupRoom;
}

//-----------------------------------------------------------------------------

Room *MainWindow::createPltEditRoom() {
  Room *pltEditRoom = new Room(this);
  pltEditRoom->setName("PltEdit");
  pltEditRoom->setObjectName("PltEditRoom");

  m_topBar->getRoomTabWidget()->addTab(tr("PltEdit"));

  DockLayout *layout = pltEditRoom->dockLayout();

  // Viewer
  TPanel *viewer = TPanelFactory::createPanel(pltEditRoom, "ComboViewer");
  if (viewer) {
    pltEditRoom->addDockWidget(viewer);
    layout->dockItem(viewer);

    ComboViewerPanel *cvp = qobject_cast<ComboViewerPanel *>(viewer);
    if (cvp) cvp->setVisiblePartsFlag(VPPARTS_TOOLBAR | VPPARTS_TOOLOPTIONS);
  }

  // Palette
  TPanel *palettePane = TPanelFactory::createPanel(pltEditRoom, "LevelPalette");
  if (palettePane) {
    pltEditRoom->addDockWidget(palettePane);
    layout->dockItem(palettePane, viewer, Region::bottom);
  }

  // StyleEditor
  TPanel *styleEditorPane =
      TPanelFactory::createPanel(pltEditRoom, "StyleEditor");
  if (styleEditorPane) {
    pltEditRoom->addDockWidget(styleEditorPane);
    layout->dockItem(styleEditorPane, viewer, Region::left);
  }

  // Xsheet
  TPanel *xsheetPane = TPanelFactory::createPanel(pltEditRoom, "Xsheet");
  if (xsheetPane) {
    pltEditRoom->addDockWidget(xsheetPane);
    layout->dockItem(xsheetPane, palettePane, Region::left);
  }

  // Studio Palette
  TPanel *studioPaletteViewer =
      TPanelFactory::createPanel(pltEditRoom, "StudioPalette");
  if (studioPaletteViewer) {
    pltEditRoom->addDockWidget(studioPaletteViewer);
    layout->dockItem(studioPaletteViewer, xsheetPane, Region::left);
  }

  return pltEditRoom;
}

//-----------------------------------------------------------------------------

Room *MainWindow::createInknPaintRoom() {
  Room *inknPaintRoom = new Room(this);
  inknPaintRoom->setName("InknPaint");
  inknPaintRoom->setObjectName("InknPaintRoom");

  m_topBar->getRoomTabWidget()->addTab(tr("InknPaint"));

  DockLayout *layout = inknPaintRoom->dockLayout();

  // Viewer
  TPanel *viewer = TPanelFactory::createPanel(inknPaintRoom, "ComboViewer");
  if (viewer) {
    inknPaintRoom->addDockWidget(viewer);
    layout->dockItem(viewer);
  }

  // Palette
  TPanel *palettePane =
      TPanelFactory::createPanel(inknPaintRoom, "LevelPalette");
  if (palettePane) {
    inknPaintRoom->addDockWidget(palettePane);
    layout->dockItem(palettePane, viewer, Region::bottom);
  }

  // Filmstrip
  TPanel *filmStripPane =
      TPanelFactory::createPanel(inknPaintRoom, "FilmStrip");
  if (filmStripPane) {
    inknPaintRoom->addDockWidget(filmStripPane);
    layout->dockItem(filmStripPane, viewer, Region::right);
  }

  return inknPaintRoom;
}

//-----------------------------------------------------------------------------

Room *MainWindow::createXsheetRoom() {
  Room *xsheetRoom = new Room(this);
  xsheetRoom->setName("Xsheet");
  xsheetRoom->setObjectName("XsheetRoom");

  m_topBar->getRoomTabWidget()->addTab(tr("Xsheet"));

  DockLayout *layout = xsheetRoom->dockLayout();

  // Xsheet
  TPanel *xsheetPane = TPanelFactory::createPanel(xsheetRoom, "Xsheet");
  if (xsheetPane) {
    xsheetRoom->addDockWidget(xsheetPane);
    layout->dockItem(xsheetPane);
  }

  // FunctionEditor
  TPanel *functionEditorPane =
      TPanelFactory::createPanel(xsheetRoom, "FunctionEditor");
  if (functionEditorPane) {
    xsheetRoom->addDockWidget(functionEditorPane);
    layout->dockItem(functionEditorPane, xsheetPane, Region::right);
  }

  return xsheetRoom;
}

//-----------------------------------------------------------------------------

Room *MainWindow::createBatchesRoom() {
  Room *batchesRoom = new Room(this);
  batchesRoom->setName("Batches");
  batchesRoom->setObjectName("BatchesRoom");

  m_topBar->getRoomTabWidget()->addTab("Batches");

  DockLayout *layout = batchesRoom->dockLayout();

  // Tasks
  TPanel *tasksViewer = TPanelFactory::createPanel(batchesRoom, "Tasks");
  if (tasksViewer) {
    batchesRoom->addDockWidget(tasksViewer);
    layout->dockItem(tasksViewer);
  }

  // BatchServers
  TPanel *batchServersViewer =
      TPanelFactory::createPanel(batchesRoom, "BatchServers");
  if (batchServersViewer) {
    batchesRoom->addDockWidget(batchServersViewer);
    layout->dockItem(batchServersViewer, tasksViewer, Region::right);
  }

  return batchesRoom;
}

//-----------------------------------------------------------------------------

Room *MainWindow::createBrowserRoom() {
  Room *browserRoom = new Room(this);
  browserRoom->setName("Browser");
  browserRoom->setObjectName("BrowserRoom");

  m_topBar->getRoomTabWidget()->addTab("Browser");

  DockLayout *layout = browserRoom->dockLayout();

  // Browser
  TPanel *browserPane = TPanelFactory::createPanel(browserRoom, "Browser");
  if (browserPane) {
    browserRoom->addDockWidget(browserPane);
    layout->dockItem(browserPane);
  }

  // Scene Cast
  TPanel *sceneCastPanel = TPanelFactory::createPanel(browserRoom, "SceneCast");
  if (sceneCastPanel) {
    browserRoom->addDockWidget(sceneCastPanel);
    layout->dockItem(sceneCastPanel, browserPane, Region::bottom);
  }

  return browserRoom;
}

//-----------------------------------------------------------------------------

Room *MainWindow::getCurrentRoom() const {
  return getRoom(m_stackedWidget->currentIndex());
}

//-----------------------------------------------------------------------------

void MainWindow::onUndo() {
  if (TApp::instance()->isSaveInProgress()) return;

  ToolHandle *toolH = TApp::instance()->getCurrentTool();

  // do not use undo if tool is currently in use
  if (toolH->getTool()->isUndoable()) {
    bool ret = TUndoManager::manager()->undo();
    if (!ret) DVGui::error(QObject::tr("No more Undo operations available."));
  }
}

//-----------------------------------------------------------------------------

void MainWindow::onRedo() {
  if (TApp::instance()->isSaveInProgress()) return;

  bool ret = TUndoManager::manager()->redo();
  if (!ret) DVGui::error(QObject::tr("No more Redo operations available."));
}

//-----------------------------------------------------------------------------

void MainWindow::onNewScene() {
  IoCmd::newScene();
  CommandManager *cm = CommandManager::instance();
  cm->setChecked(MI_ShiftTrace, false);
  cm->setChecked(MI_EditShift, false);
  cm->setChecked(MI_NoShift, false);
  cm->setChecked(MI_ShowShiftOrigin, false);
  cm->setChecked(MI_VectorGuidedDrawing, false);
}

//-----------------------------------------------------------------------------

void MainWindow::onLoadScene() { IoCmd::loadScene(); }

//-----------------------------------------------------------------------------

void MainWindow::onLoadSubScene() { IoCmd::loadSubScene(); }
//-----------------------------------------------------------------------------

void MainWindow::onUpgradeTabPro() {}

//-----------------------------------------------------------------------------

void MainWindow::onAbout() {
  QLabel *label  = new QLabel();
  QPixmap pixmap = QIcon(":Resources/splash.svg").pixmap(QSize(610, 344));
  pixmap.setDevicePixelRatio(getDevicePixelRatio(this));
  label->setPixmap(pixmap);

  DVGui::Dialog *dialog = new DVGui::Dialog(this, true);
  dialog->setWindowTitle(tr("About OpenToonz"));
  dialog->setTopMargin(0);
  dialog->addWidget(label);
  QHBoxLayout *hLay = new QHBoxLayout();
  {
    QString name = QString::fromStdString(TEnv::getApplicationFullName());
    name += " (built " __DATE__ " " __TIME__ ")";
    hLay->addWidget(new QLabel(name, dialog));

    QCheckBox *showDateCheckBox =
        new QCheckBox(tr("Show build date in title"), dialog);
    showDateCheckBox->setChecked(ShowBuildDateInTitle);
    connect(showDateCheckBox, &QCheckBox::stateChanged, [=](int state) {
      bool show            = (state == Qt::Checked);
      ShowBuildDateInTitle = show;
      changeWindowTitle();
    });
    hLay->addWidget(showDateCheckBox);
  }
  dialog->addLayout(hLay);

  QPushButton *button = new QPushButton(tr("Close"), dialog);
  button->setDefault(true);
  dialog->addButtonBarWidget(button);
  connect(button, SIGNAL(clicked()), dialog, SLOT(accept()));
  dialog->exec();
}

//-----------------------------------------------------------------------------

void MainWindow::onOpenOnlineManual() {
  QDesktopServices::openUrl(QUrl(tr("http://opentoonz.readthedocs.io")));
}

//-----------------------------------------------------------------------------

void MainWindow::onOpenWhatsNew() {
  QDesktopServices::openUrl(
      QUrl(tr("https://github.com/opentoonz/opentoonz/releases/latest")));
}

//-----------------------------------------------------------------------------

void MainWindow::onOpenQuicklink() { CustomHelpLink::open(); }

//-----------------------------------------------------------------------------

void MainWindow::onOpenCommunityForum() {
  QDesktopServices::openUrl(
      QUrl(tr("https://groups.google.com/forum/#!forum/opentoonz_en")));
}

//-----------------------------------------------------------------------------

void MainWindow::onOpenReportABug() {
  QString str = QString(
      tr("To report a bug, click on the button below to open a web browser "
         "window for OpenToonz's Issues page on https://github.com.  Click on "
         "the 'New issue' button and fill out the form."));

  std::vector<QString> buttons = {QO…23285 tokens truncated…oolCommandType);
  createAction(MI_TapeNormal, QT_TR_NOOP("Tape Tool - Normal"), "",
               ToolCommandType, "tape_normal");
  createAction(MI_TapeRectangular, QT_TR_NOOP("Tape Tool - Rectangular"), "",
               ToolCommandType, "tape_rectangular");
  createAction(MI_TapeNextMode, QT_TR_NOOP("Tape Tool - Next Mode"), "",
               ToolCommandType);
  createAction(MI_TapeEndpointToEndpoint,
               QT_TR_NOOP("Tape Tool - Endpoint to Endpoint"), "",
               ToolCommandType, "tape_end_to_end");
  createAction(MI_TapeEndpointToLine,
               QT_TR_NOOP("Tape Tool - Endpoint to Line"), "", ToolCommandType,
               "tape_end_to_line");
  createAction(MI_TapeLineToLine, QT_TR_NOOP("Tape Tool - Line to Line"), "",
               ToolCommandType, "tape_line_to_line");

  /*-- Style Picker tool + mode switching shortcuts --*/
  createAction(MI_PickStyleNextMode,
               QT_TR_NOOP("Style Picker Tool - Next Mode"), "",
               ToolCommandType);
  createAction(MI_PickStyleAreas, QT_TR_NOOP("Style Picker Tool - Areas"), "",
               ToolCommandType, "stylepicker_areas");
  createAction(MI_PickStyleLines, QT_TR_NOOP("Style Picker Tool - Lines"), "",
               ToolCommandType, "stylepicker_lines");
  createAction(MI_PickStyleLinesAndAreas,
               QT_TR_NOOP("Style Picker Tool - Lines & Areas"), "",
               ToolCommandType, "stylepicker_lines_areas");

  /*-- RGB Picker tool + type switching shortcuts --*/
  createAction(MI_RGBPickerNextType, QT_TR_NOOP("RGB Picker Tool - Next Type"),
               "", ToolCommandType);
  createAction(MI_RGBPickerNormal, QT_TR_NOOP("RGB Picker Tool - Normal"), "",
               ToolCommandType);
  createAction(MI_RGBPickerRectangular,
               QT_TR_NOOP("RGB Picker Tool - Rectangular"), "",
               ToolCommandType);
  createAction(MI_RGBPickerFreehand, QT_TR_NOOP("RGB Picker Tool - Freehand"),
               "", ToolCommandType);
  createAction(MI_RGBPickerPolyline, QT_TR_NOOP("RGB Picker Tool - Polyline"),
               "", ToolCommandType);

  /*-- Skeleton tool + mode switching shortcuts --*/
  createAction(MI_SkeletonNextMode, QT_TR_NOOP("Skeleton Tool - Next Mode"), "",
               ToolCommandType);
  createAction(MI_SkeletonBuildSkeleton,
               QT_TR_NOOP("Skeleton Tool - Build Skeleton"), "",
               ToolCommandType);
  createAction(MI_SkeletonAnimate, QT_TR_NOOP("Skeleton Tool - Animate"), "",
               ToolCommandType);
  createAction(MI_SkeletonInverseKinematics,
               QT_TR_NOOP("Skeleton Tool - Inverse Kinematics"), "",
               ToolCommandType);

  /*-- Plastic tool + mode switching shortcuts --*/
  createAction(MI_PlasticNextMode, QT_TR_NOOP("Plastic Tool - Next Mode"), "",
               ToolCommandType);
  createAction(MI_PlasticEditMesh, QT_TR_NOOP("Plastic Tool - Edit Mesh"), "",
               ToolCommandType);
  createAction(MI_PlasticPaintRigid, QT_TR_NOOP("Plastic Tool - Paint Rigid"),
               "", ToolCommandType);
  createAction(MI_PlasticBuildSkeleton,
               QT_TR_NOOP("Plastic Tool - Build Skeleton"), "",
               ToolCommandType);
  createAction(MI_PlasticAnimate, QT_TR_NOOP("Plastic Tool - Animate"), "",
               ToolCommandType);

  /*-- Edit Assistants tool + type switching shortcuts --*/
  createAction(MI_AssistantNextType,
               QT_TR_NOOP("Assistant Type - Cycle"), "", ToolCommandType);
  createAction(MI_AssistantLine, QT_TR_NOOP("Assistant Type - Line"), "",
               ToolCommandType, "assistant_line");
  createAction(MI_AssistantEllipse, QT_TR_NOOP("Assistant Type - Ellipse"), "",
               ToolCommandType, "assistant_ellipse");
  createAction(MI_AssistantPerspective,
               QT_TR_NOOP("Assistant Type - Perspective"), "",
               ToolCommandType, "assistant_perspective");
  createAction(MI_AssistantVanishingPoint,
               QT_TR_NOOP("Assistant Type - Vanishing Point"), "",
               ToolCommandType, "assistant_vanishingpoint");
  createAction(MI_AssistantFisheye, QT_TR_NOOP("Assistant Type - Fisheye"), "",
               ToolCommandType, "assistant_fisheye");
  createAction(MI_AssistantReplicatorStar,
               QT_TR_NOOP("Assistant Type - Replicator Star"), "",
               ToolCommandType, "replicator_star");
  createAction(MI_AssistantReplicatorMirror,
               QT_TR_NOOP("Assistant Type - Replicator Mirror"), "",
               ToolCommandType, "replicator_mirror");
  createAction(MI_AssistantReplicatorJitter,
               QT_TR_NOOP("Assistant Type - Replicator Jitter"), "",
               ToolCommandType, "replicator_jitter");
  createAction(MI_AssistantReplicatorGrid,
               QT_TR_NOOP("Assistant Type - Replicator Grid"), "",
               ToolCommandType, "replicator_grid");
  createAction(MI_AssistantReplicatorAffine,
               QT_TR_NOOP("Assistant Type - Replicator Affine"), "",
               ToolCommandType, "replicator_affine");

  // Tool Modifiers

  createToolOptionsAction(MI_SelectNextGuideStroke,
                          QT_TR_NOOP("Select Next Frame Guide Stroke"), "");
  createToolOptionsAction(MI_SelectPrevGuideStroke,
                          QT_TR_NOOP("Select Previous Frame Guide Stroke"), "");
  createToolOptionsAction(
      MI_SelectBothGuideStrokes,
      QT_TRANSLATE_NOOP("MainWindow",
                        "Select Prev && Next Frame Guide Strokes"),
      "");
  createToolOptionsAction(MI_SelectGuideStrokeReset,
                          QT_TR_NOOP("Reset Guide Stroke Selections"), "");
  createToolOptionsAction(MI_TweenGuideStrokes,
                          QT_TR_NOOP("Tween Selected Guide Strokes"), "");
  createToolOptionsAction(MI_TweenGuideStrokeToSelected,
                          QT_TR_NOOP("Tween Guide Strokes to Selected"), "");
  createToolOptionsAction(MI_SelectGuidesAndTweenMode,
                          QT_TR_NOOP("Select Guide Strokes && Tween Mode"), "");
  createToolOptionsAction(MI_FlipNextGuideStroke,
                          QT_TR_NOOP("Flip Next Guide Stroke Direction"), "");
  createToolOptionsAction(MI_FlipPrevGuideStroke,
                          QT_TR_NOOP("Flip Previous Guide Stroke Direction"),
                          "");
  createToolOptionsAction("A_ToolOption_GlobalKey", QT_TR_NOOP("Global Key"),
                          "");

  createToolOptionsAction("A_IncreaseMaxBrushThickness",
                          QT_TR_NOOP("Brush size - Increase max"), "]");
  createToolOptionsAction("A_DecreaseMaxBrushThickness",
                          QT_TR_NOOP("Brush size - Decrease max"), "[");
  createToolOptionsAction("A_IncreaseMinBrushThickness",
                          QT_TR_NOOP("Brush size - Increase min"), "Shift+]");
  createToolOptionsAction("A_DecreaseMinBrushThickness",
                          QT_TR_NOOP("Brush size - Decrease min"), "Shift+[");
  createToolOptionsAction("A_IncreaseBrushHardness",
                          QT_TR_NOOP("Brush hardness - Increase"), "Ctrl+]");
  createToolOptionsAction("A_DecreaseBrushHardness",
                          QT_TR_NOOP("Brush hardness - Decrease"), "Ctrl+[");
  createToolOptionsAction("A_ToolOption_SnapSensitivity",
                          QT_TR_NOOP("Snap Sensitivity"), "");
  createToolOptionsAction("A_ToolOption_AutoGroup", QT_TR_NOOP("Auto Group"),
                          "");
  createToolOptionsAction("A_ToolOption_BreakSharpAngles",
                          QT_TR_NOOP("Break sharp angles"), "");
  createToolOptionsAction("A_ToolOption_FrameRange", QT_TR_NOOP("Frame range"),
                          "F6");
  createToolOptionsAction("A_ToolOption_IK", QT_TR_NOOP("Inverse Kinematics"),
                          "");
  createToolOptionsAction("A_ToolOption_Invert", QT_TR_NOOP("Invert"), "");
  createToolOptionsAction("A_ToolOption_Manual", QT_TR_NOOP("Manual"), "");
  createToolOptionsAction("A_ToolOption_OnionSkin", QT_TR_NOOP("Onion skin"),
                          "");
  createToolOptionsAction("A_ToolOption_Orientation", QT_TR_NOOP("Orientation"),
                          "");
  createToolOptionsAction("A_ToolOption_PencilMode", QT_TR_NOOP("Pencil Mode"),
                          "");
  createToolOptionsAction("A_ToolOption_PreserveThickness",
                          QT_TR_NOOP("Preserve Thickness"), "");
  createToolOptionsAction("A_ToolOption_PressureSensitivity",
                          QT_TR_NOOP("Pressure Sensitivity"), "Shift+P");
  createToolOptionsAction("A_ToolOption_SegmentInk", QT_TR_NOOP("Segment Ink"),
                          "F8");
  createToolOptionsAction("A_ToolOption_EmptyOnly", QT_TR_NOOP("Empty Only"),
                          "F7");
  createToolOptionsAction("A_ToolOption_Selective", QT_TR_NOOP("Selective"),
                          "F9");
  createToolOptionsAction("A_ToolOption_DrawOrder",
                          QT_TR_NOOP("Brush Tool - Draw Order"), "");
  createToolOptionsAction("A_ToolOption_Smooth", QT_TR_NOOP("Smooth"), "");
  createToolOptionsAction("A_ToolOption_Snap", QT_TR_NOOP("Snap"), "");
  createToolOptionsAction("A_ToolOption_AutoSelectDrawing",
                          QT_TR_NOOP("Auto Select Drawing"), "");
  createToolOptionsAction("A_ToolOption_Autofill", QT_TR_NOOP("Auto Fill"), "");
  createToolOptionsAction("A_ToolOption_JoinVectors",
                          QT_TR_NOOP("Join Vectors"), "");
  createToolOptionsAction("A_ToolOption_ShowOnlyActiveSkeleton",
                          QT_TR_NOOP("Show Only Active Skeleton"), "");
  createToolOptionsAction("A_ToolOption_RasterEraser",
                          QT_TR_NOOP("Brush Tool - Eraser (Raster option)"),
                          "");
  createToolOptionsAction("A_ToolOption_LockAlpha",
                          QT_TR_NOOP("Brush Tool - Lock Alpha"), "");
  createToolOptionsAction("A_ToolOption_BrushPreset",
                          QT_TR_NOOP("Brush Preset"), "");
  createToolOptionsAction("A_ToolOption_GeometricShape",
                          QT_TR_NOOP("Geometric Shape"), "");
  createToolOptionsAction("A_ToolOption_GeometricShape:Rectangle",
                          QT_TR_NOOP("Geometric Shape Rectangle"), "");
  createToolOptionsAction("A_ToolOption_GeometricShape:Circle",
                          QT_TR_NOOP("Geometric Shape Circle"), "");
  createToolOptionsAction("A_ToolOption_GeometricShape:Ellipse",
                          QT_TR_NOOP("Geometric Shape Ellipse"), "");
  createToolOptionsAction("A_ToolOption_GeometricShape:Line",
                          QT_TR_NOOP("Geometric Shape Line"), "");
  createToolOptionsAction("A_ToolOption_GeometricShape:Polyline",
                          QT_TR_NOOP("Geometric Shape Polyline"), "");
  createToolOptionsAction("A_ToolOption_GeometricShape:Arc",
                          QT_TR_NOOP("Geometric Shape Arc"), "");
  createToolOptionsAction("A_ToolOption_GeometricShape:MultiArc",
                          QT_TR_NOOP("Geometric Shape MultiArc"), "");
  createToolOptionsAction("A_ToolOption_GeometricShape:Polygon",
                          QT_TR_NOOP("Geometric Shape Polygon"), "");
  createToolOptionsAction("A_ToolOption_GeometricEdge",
                          QT_TR_NOOP("Geometric Edge"), "");
  createToolOptionsAction("A_ToolOption_AssistantType",
                          QT_TR_NOOP("Assistant Type"), "");
  createToolOptionsAction("A_ToolOption_AssistantType:assistantLine",
                          QT_TR_NOOP("Assistant Type - Line"), "");
  createToolOptionsAction("A_ToolOption_AssistantType:assistantEllipse",
                          QT_TR_NOOP("Assistant Type - Ellipse"), "");
  createToolOptionsAction("A_ToolOption_AssistantType:assistantPerspective",
                          QT_TR_NOOP("Assistant Type - Perspective"), "");
  createToolOptionsAction(
      "A_ToolOption_AssistantType:assistantVanishingPoint",
      QT_TR_NOOP("Assistant Type - Vanishing Point"), "");
  createToolOptionsAction("A_ToolOption_AssistantType:assistantFisheye",
                          QT_TR_NOOP("Assistant Type - Fisheye"), "");
  createToolOptionsAction("A_ToolOption_AssistantType:replicatorStar",
                          QT_TR_NOOP("Assistant Type - Replicator Star"), "");
  createToolOptionsAction("A_ToolOption_AssistantType:replicatorMirror",
                          QT_TR_NOOP("Assistant Type - Replicator Mirror"), "");
  createToolOptionsAction("A_ToolOption_AssistantType:replicatorJitter",
                          QT_TR_NOOP("Assistant Type - Replicator Jitter"), "");
  createToolOptionsAction("A_ToolOption_AssistantType:replicatorGrid",
                          QT_TR_NOOP("Assistant Type - Replicator Grid"), "");
  createToolOptionsAction("A_ToolOption_AssistantType:replicatorAffine",
                          QT_TR_NOOP("Assistant Type - Replicator Affine"), "");
  createToolOptionsAction("A_ToolOption_Mode", QT_TR_NOOP("Mode"), "");
  menuAct = createToolOptionsAction(
      "A_ToolOption_Mode:Areas", QT_TR_NOOP("Mode - Areas"), "", "mode_areas");
  menuAct = createToolOptionsAction(
      "A_ToolOption_Mode:Lines", QT_TR_NOOP("Mode - Lines"), "", "mode_lines");
  menuAct = createToolOptionsAction("A_ToolOption_Mode:Lines & Areas",
                                    QT_TR_NOOP("Mode - Lines && Areas"), "",
                                    "mode_areas_lines");
  createToolOptionsAction("A_ToolOption_Mode:Endpoint to Endpoint",
                          QT_TR_NOOP("Mode - Endpoint to Endpoint"), "");
  createToolOptionsAction("A_ToolOption_Mode:Endpoint to Line",
                          QT_TR_NOOP("Mode - Endpoint to Line"), "");
  createToolOptionsAction("A_ToolOption_Mode:Line to Line",
                          QT_TR_NOOP("Mode - Line to Line"), "");
  createToolOptionsAction("A_ToolOption_Type", QT_TR_NOOP("Type"), "");

  menuAct =
      createToolOptionsAction("A_ToolOption_Type:Normal",
                              QT_TR_NOOP("Type - Normal"), "", "type_normal");
  menuAct = createToolOptionsAction("A_ToolOption_Type:Rectangular",
                                    QT_TR_NOOP("Type - Rectangular"), "F5",
                                    "type_rectangular");
  menuAct =
      createToolOptionsAction("A_ToolOption_Type:Freehand",
                              QT_TR_NOOP("Type - Freehand"), "", "type_lasso");
  menuAct = createToolOptionsAction("A_ToolOption_Type:Polyline",
                                    QT_TR_NOOP("Type - Polyline"), "",
                                    "type_polyline");
  menuAct = createToolOptionsAction("A_ToolOption_Type:Freepick",
                                    QT_TR_NOOP("Type - Pick+Freehand"), "",
                                    "type_pickerlasso");
  menuAct = createToolOptionsAction("A_ToolOption_Type:Segment",
                                    QT_TR_NOOP("Type - Segment"), "",
                                    "type_erase_segment");
  menuAct = createToolOptionsAction("A_ToolOption_Type:MultiArc",
                                    QT_TR_NOOP("Eraser Type - MultiArc"), "",
                                    "type_erase_multiarc");

  createToolOptionsAction("A_ToolOption_TypeFont", QT_TR_NOOP("TypeTool Font"),
                          "");
  createToolOptionsAction("A_ToolOption_TypeSize", QT_TR_NOOP("TypeTool Size"),
                          "");
  createToolOptionsAction("A_ToolOption_TypeStyle",
                          QT_TR_NOOP("TypeTool Style"), "");
  createToolOptionsAction("A_ToolOption_TypeStyle:Oblique",
                          QT_TR_NOOP("TypeTool Style - Oblique"), "");
  createToolOptionsAction("A_ToolOption_TypeStyle:Regular",
                          QT_TR_NOOP("TypeTool Style - Regular"), "");
  createToolOptionsAction("A_ToolOption_TypeStyle:Bold Oblique",
                          QT_TR_NOOP("TypeTool Style - Bold Oblique"), "");
  createToolOptionsAction("A_ToolOption_TypeStyle:Bold",
                          QT_TR_NOOP("TypeTool Style - Bold"), "");

  createToolOptionsAction("A_ToolOption_EditToolActiveAxis",
                          QT_TR_NOOP("Active Axis"), "");
  createToolOptionsAction("A_ToolOption_EditToolActiveAxis:Position",
                          QT_TR_NOOP("Active Axis - Position"), "");
  createToolOptionsAction("A_ToolOption_EditToolActiveAxis:Rotation",
                          QT_TR_NOOP("Active Axis - Rotation"), "");
  createToolOptionsAction("A_ToolOption_EditToolActiveAxis:Scale",
                          QT_TR_NOOP("Active Axis - Scale"), "");
  createToolOptionsAction("A_ToolOption_EditToolActiveAxis:Shear",
                          QT_TR_NOOP("Active Axis - Shear"), "");
  createToolOptionsAction("A_ToolOption_EditToolActiveAxis:Center",
                          QT_TR_NOOP("Active Axis - Center"), "");
  createToolOptionsAction("A_ToolOption_EditToolActiveAxis:All",
                          QT_TR_NOOP("Active Axis - All"), "");

  createToolOptionsAction("A_ToolOption_SkeletonMode",
                          QT_TR_NOOP("Skeleton Mode"), "");
  createToolOptionsAction("A_ToolOption_SkeletonMode:Edit Mesh",
                          QT_TR_NOOP("Edit Mesh Mode"), "");
  createToolOptionsAction("A_ToolOption_SkeletonMode:Paint Rigid",
                          QT_TR_NOOP("Paint Rigid Mode"), "");
  createToolOptionsAction("A_ToolOption_SkeletonMode:Build Skeleton",
                          QT_TR_NOOP("Build Skeleton Mode"), "");
  createToolOptionsAction("A_ToolOption_SkeletonMode:Animate",
                          QT_TR_NOOP("Animate Mode"), "");
  createToolOptionsAction("A_ToolOption_SkeletonMode:Inverse Kinematics",
                          QT_TR_NOOP("Inverse Kinematics Mode"), "");
  createToolOptionsAction("A_ToolOption_AutoSelect:None",
                          QT_TR_NOOP("None Pick Mode"), "");
  createToolOptionsAction("A_ToolOption_AutoSelect:Column",
                          QT_TR_NOOP("Column Pick Mode"), "");
  createToolOptionsAction("A_ToolOption_AutoSelect:Pegbar",
                          QT_TR_NOOP("Pegbar Pick Mode"), "");
  menuAct = createToolOptionsAction("A_ToolOption_PickScreen",
                                    QT_TR_NOOP("Pick Screen"), "");
  menuAct->setIcon(createQIcon("pickscreen"));
  createToolOptionsAction("A_ToolOption_Meshify", QT_TR_NOOP("Create Mesh"),
                          "");

  menuAct =
      createToolOptionsAction("A_ToolOption_AutopaintLines",
                              QT_TR_NOOP("Fill Tool - Autopaint Lines"), "");
  menuAct->setIcon(createQIcon("toggle_autofill"));

  createToolOptionsAction("A_ToolOption_FlipHorizontal",
                          QT_TR_NOOP("Flip Selection/Object Horizontally"), "");
  createToolOptionsAction("A_ToolOption_FlipVertical",
                          QT_TR_NOOP("Flip Selection/Object Vertically"), "");
  createToolOptionsAction("A_ToolOption_RotateLeft",
                          QT_TR_NOOP("Rotate Selection/Object Left"), "");
  createToolOptionsAction("A_ToolOption_RotateRight",
                          QT_TR_NOOP("Rotate Selection/Object Right"), "");

  // Visualization

  createViewerAction(V_ZoomIn, QT_TR_NOOP("Zoom In"), "+");
  createViewerAction(V_ZoomOut, QT_TR_NOOP("Zoom Out"), "-");
  createViewerAction(V_ViewReset, QT_TR_NOOP("Reset View"), "Alt+0");
  createViewerAction(V_ZoomFit, QT_TR_NOOP("Fit to Window"), "Alt+9");
  createViewerAction(V_ZoomReset, QT_TR_NOOP("Reset Zoom"), "");
  createViewerAction(V_RotateReset, QT_TR_NOOP("Reset Rotation"), "");
  createViewerAction(V_PositionReset, QT_TR_NOOP("Reset Position"), "");

  createViewerAction(V_ActualPixelSize, QT_TR_NOOP("Actual Pixel Size"), "N");
  createViewerAction(V_FlipX, QT_TR_NOOP("Flip Viewer Horizontally"), "");
  createViewerAction(V_FlipY, QT_TR_NOOP("Flip Viewer Vertically"), "");
  createViewerAction(V_RotateLeft, QT_TR_NOOP("Rotate Viewer Left"), "");
  createViewerAction(V_RotateRight, QT_TR_NOOP("Rotate Viewer Right"), "");
  createViewerAction(V_ShowHideFullScreen, QT_TR_NOOP("Show//Hide Full Screen"),
                     "Alt+F");
  CommandManager::instance()->setToggleTexts(V_ShowHideFullScreen,
                                             tr("Full Screen Mode"),
                                             tr("Exit Full Screen Mode"));
  createViewerAction(MI_CompareToSnapshot, QT_TR_NOOP("Compare to Snapshot"),
                     "");
  createViewerAction(MI_ZoomInAndFitPanel,
                     QT_TR_NOOP("Zoom In And Fit Floating Panel"),
                     "Ctrl+Alt++");
  createViewerAction(MI_ZoomOutAndFitPanel,
                     QT_TR_NOOP("Zoom Out And Fit Floating Panel"),
                     "Ctrl+Alt+-");

  // Following actions are for adding "Visualization" menu items to the command
  // bar. They are separated from the original actions in order to avoid
  // assigning shortcut keys. They must be triggered only from pressing buttons
  // in the command bar. Assigning shortcut keys and registering as MenuItem
  // will break a logic of ShortcutZoomer. So here we register separate items
  // and bypass the command.
  createVisualizationButtonAction(VB_ViewReset, QT_TR_NOOP("Reset View"),
                                  "reset");
  createVisualizationButtonAction(VB_ZoomFit, QT_TR_NOOP("Fit to Window"),
                                  "fit_to_window");
  createVisualizationButtonAction(VB_ZoomReset, QT_TR_NOOP("Reset Zoom"),
                                  "zoom_reset");
  createVisualizationButtonAction(VB_RotateReset, QT_TR_NOOP("Reset Rotation"),
                                  "rotate_reset");
  createVisualizationButtonAction(VB_PositionReset,
                                  QT_TR_NOOP("Reset Position"));
  createVisualizationButtonAction(
      VB_ActualPixelSize, QT_TR_NOOP("Actual Pixel Size"), "actual_pixel_size");
  createVisualizationButtonAction(
      VB_FlipX, QT_TR_NOOP("Flip Viewer Horizontally"), "fliphoriz");
  createVisualizationButtonAction(
      VB_FlipY, QT_TR_NOOP("Flip Viewer Vertically"), "flipvert");
  createVisualizationButtonAction(VB_RotateLeft, QT_TR_NOOP("Rotate View Left"),
                                  "rotateleft");
  createVisualizationButtonAction(
      VB_RotateRight, QT_TR_NOOP("Rotate View Right"), "rotateright");

  // Misc

  menuAct =
      createToggle(MI_TouchGestureControl, QT_TR_NOOP("&Touch Gesture Control"),
                   "", TouchGestureControl ? 1 : 0, MiscCommandType, "touch");
  menuAct->setEnabled(true);
  ;
  createMiscAction(MI_CameraStage, QT_TR_NOOP("&Camera Settings..."), "");
  menuAct =
      createMiscAction(MI_RefreshTree, QT_TR_NOOP("Refresh Folder Tree"), "");
  menuAct->setIconText(tr("Refresh"));
  createMiscAction("A_FxSchematicToggle",
                   QT_TR_NOOP("Toggle FX/Stage schematic"), "");

  // RGBA

  createRGBAAction(MI_RedChannel, QT_TR_NOOP("Red Channel"), "");
  createRGBAAction(MI_GreenChannel, QT_TR_NOOP("Green Channel"), "");
  createRGBAAction(MI_BlueChannel, QT_TR_NOOP("Blue Channel"), "");
  createRGBAAction(MI_MatteChannel, QT_TR_NOOP("Alpha Channel"), "");
  createRGBAAction(MI_RedChannelGreyscale, QT_TR_NOOP("Red Channel Greyscale"),
                   "");
  createRGBAAction(MI_GreenChannelGreyscale,
                   QT_TR_NOOP("Green Channel Greyscale"), "");
  createRGBAAction(MI_BlueChannelGreyscale,
                   QT_TR_NOOP("Blue Channel Greyscale"), "");

  // Stop Motion

#if defined(x64)
  createStopMotionAction(MI_StopMotionExportImageSequence,
                         QT_TR_NOOP("&Export Stop Motion Image Sequence"), "");
  createStopMotionAction(MI_StopMotionCapture,
                         QT_TR_NOOP("Capture Stop Motion Frame"), "");
  createStopMotionAction(MI_StopMotionRaiseOpacity,
                         QT_TR_NOOP("Raise Stop Motion Opacity"), "");
  createStopMotionAction(MI_StopMotionLowerOpacity,
                         QT_TR_NOOP("Lower Stop Motion Opacity"), "");
  createStopMotionAction(MI_StopMotionToggleLiveView,
                         QT_TR_NOOP("Toggle Stop Motion Live View"), "");
#ifdef WITH_CANON
  createStopMotionAction(MI_StopMotionToggleZoom,
                         QT_TR_NOOP("Toggle Stop Motion Zoom"), "");
  createStopMotionAction(MI_StopMotionPickFocusCheck,
                         QT_TR_NOOP("Pick Focus Check Location"), "");
#endif  // WITH_CANON
  createStopMotionAction(MI_StopMotionLowerSubsampling,
                         QT_TR_NOOP("Lower Stop Motion Level Subsampling"), "");
  createStopMotionAction(MI_StopMotionRaiseSubsampling,
                         QT_TR_NOOP("Raise Stop Motion Level Subsampling"), "");
  createStopMotionAction(MI_StopMotionJumpToCamera,
                         QT_TR_NOOP("Go to Stop Motion Insert Frame"), "");
  createStopMotionAction(MI_StopMotionRemoveFrame,
                         QT_TR_NOOP("Remove frame before Stop Motion Camera"),
                         "");
  createStopMotionAction(MI_StopMotionNextFrame,
                         QT_TR_NOOP("Next Frame including Stop Motion Camera"),
                         "");
  createStopMotionAction(MI_StopMotionToggleUseLiveViewImages,
                         QT_TR_NOOP("Show original live view images."), "");
#endif  // x64

  // Special Modifier Keys
  createSpecialModifierAction(V_Scrub, QT_TR_NOOP("Viewer Scrub"), "#");

  // create cell mark actions
  for (int markId = 0; markId < 12; markId++) {
    std::string cmdId = (std::string)MI_SetCellMark + std::to_string(markId);
    std::string labelStr =
        QT_TR_NOOP("Set Cell Mark ") + std::to_string(markId);
    QAction *action =
        createAction(cmdId.c_str(), labelStr.c_str(), "", CellMarkCommandType);
    action->setData(markId);
  }
}

//-----------------------------------------------------------------------------

void MainWindow::onInkCheckTriggered(bool on) {
  if (!on) return;
  QAction *ink1CheckAction =
      CommandManager::instance()->getAction(MI_Ink1Check);
  if (ink1CheckAction) ink1CheckAction->setChecked(false);
}

//-----------------------------------------------------------------------------

void MainWindow::onInk1CheckTriggered(bool on) {
  if (!on) return;
  QAction *inkCheckAction = CommandManager::instance()->getAction(MI_ICheck);
  if (inkCheckAction) inkCheckAction->setChecked(false);
}

//-----------------------------------------------------------------------------

void MainWindow::onNewVectorLevelButtonPressed() {
  int defaultLevelType = Preferences::instance()->getDefLevelType();
  Preferences::instance()->setValue(DefLevelType, PLI_XSHLEVEL);
  CommandManager::instance()->execute("MI_NewLevel");
  Preferences::instance()->setValue(DefLevelType, defaultLevelType);
}

//-----------------------------------------------------------------------------

void MainWindow::onNewToonzRasterLevelButtonPressed() {
  int defaultLevelType = Preferences::instance()->getDefLevelType();
  Preferences::instance()->setValue(DefLevelType, TZP_XSHLEVEL);
  CommandManager::instance()->execute("MI_NewLevel");
  Preferences::instance()->setValue(DefLevelType, defaultLevelType);
}

//-----------------------------------------------------------------------------

void MainWindow::onNewRasterLevelButtonPressed() {
  int defaultLevelType = Preferences::instance()->getDefLevelType();
  Preferences::instance()->setValue(DefLevelType, OVL_XSHLEVEL);
  CommandManager::instance()->execute("MI_NewLevel");
  Preferences::instance()->setValue(DefLevelType, defaultLevelType);
}

//-----------------------------------------------------------------------------
// delete unused files / folders in the cache
void MainWindow::clearCacheFolder() {
  // currently cache folder is used for following purposes
  // 1. $CACHE/[ProcessID] : for disk swap of image cache.
  //    To be deleted on exit. Remains on crash.
  // 2. $CACHE/ffmpeg : ffmpeg cache.
  //    To be cleared on the end of rendering, on exist and on launch.
  // 3. $CACHE/temp : untitled scene data.
  //    To be deleted on switching or exiting scenes. Remains on crash.

  // So, this function will delete all files / folders in $CACHE
  // except the following items:
  // 1. $CACHE/[Current ProcessID]
  // 2. $CACHE/temp/[Current scene folder] if the current scene is untitled

  TFilePath cacheRoot = ToonzFolder::getCacheRootFolder();
  if (cacheRoot.isEmpty()) cacheRoot = TEnv::getStuffDir() + "cache";

  TFilePathSet filesToBeRemoved;

  TSystem::readDirectory(filesToBeRemoved, cacheRoot, false);

  // keep the imagecache folder
  filesToBeRemoved.remove(cacheRoot + std::to_string(TSystem::getProcessId()));
  // keep the untitled scene data folder
  if (TApp::instance()->getCurrentScene()->getScene()->isUntitled()) {
    filesToBeRemoved.remove(cacheRoot + "temp");
    TFilePathSet untitledData =
        TSystem::readDirectory(cacheRoot + "temp", false);
    untitledData.remove(TApp::instance()
                            ->getCurrentScene()
                            ->getScene()
                            ->getScenePath()
                            .getParentDir());
    filesToBeRemoved.insert(filesToBeRemoved.end(), untitledData.begin(),
                            untitledData.end());
  }

  // return if there is no files/folders to be deleted
  if (filesToBeRemoved.size() == 0) {
    QMessageBox::information(
        this, tr("Clear Cache Folder"),
        tr("There are no unused items in the cache folder."));
    return;
  }

  QString message(tr("Deleting the following items:\n"));
  int count = 0;
  for (const auto &fileToBeRemoved : filesToBeRemoved) {
    QString dirPrefix =
        (TFileStatus(fileToBeRemoved).isDirectory()) ? tr("<DIR> ") : "";
    message +=
        "   " + dirPrefix + (fileToBeRemoved - cacheRoot).getQString() + "\n";
    count++;
    if (count == 5) break;
  }
  if (filesToBeRemoved.size() > 5)
    message +=
        tr("   ... and %1 more items\n").arg(filesToBeRemoved.size() - 5);

  message +=
      tr("\nAre you sure?\n\nN.B. Make sure you are not running another "
         "process of OpenToonz,\nor you may delete necessary files for it.");

  QMessageBox::StandardButton ret = QMessageBox::question(
      this, tr("Clear Cache Folder"), message,
      QMessageBox::StandardButtons(QMessageBox::Ok | QMessageBox::Cancel));

  if (ret != QMessageBox::Ok) return;

  for (const auto &fileToBeRemoved : filesToBeRemoved) {
    try {
      if (TFileStatus(fileToBeRemoved).isDirectory())
        TSystem::rmDirTree(fileToBeRemoved);
      else
        TSystem::deleteFile(fileToBeRemoved);
    } catch (TException &e) {
      QMessageBox::warning(
          this, tr("Clear Cache Folder"),
          tr("Can't delete %1 : ").arg(fileToBeRemoved.getQString()) +
              QString::fromStdWString(e.getMessage()));
    } catch (...) {
      QMessageBox::warning(
          this, tr("Clear Cache Folder"),
          tr("Can't delete %1 : ").arg(fileToBeRemoved.getQString()));
    }
  }
}

//-----------------------------------------------------------------------------

void MainWindow::onNewMetaLevelButtonPressed() {
  int defaultLevelType = Preferences::instance()->getDefLevelType();
  Preferences::instance()->setValue(DefLevelType, META_XSHLEVEL);
  CommandManager::instance()->execute("MI_NewLevel");
  Preferences::instance()->setValue(DefLevelType, defaultLevelType);
}

//-----------------------------------------------------------------------------

class ReloadStyle final : public MenuItemHandler {
public:
  ReloadStyle() : MenuItemHandler("MI_ReloadStyle") {}
  void execute() override {
    QString currentStyle = Preferences::instance()->getCurrentStyleSheet();
    qApp->setStyleSheet(currentStyle);
  }
} reloadStyle;

void MainWindow::onQuit() { close(); }

//=============================================================================
// RecentFiles
//=============================================================================

RecentFiles::RecentFiles()
    : m_recentScenes(), m_recentSceneProjects(), m_recentLevels() {}

//-----------------------------------------------------------------------------

RecentFiles *RecentFiles::instance() {
  static RecentFiles _instance;
  return &_instance;
}

//-----------------------------------------------------------------------------

RecentFiles::~RecentFiles() {}

//-----------------------------------------------------------------------------

void RecentFiles::addFilePath(QString path, FileType fileType,
                              QString projectName) {
  QList<QString> files = (fileType == Scene)   ? m_recentScenes
                         : (fileType == Level) ? m_recentLevels
                                               : m_recentFlipbookImages;
  int i;
  for (i = 0; i < files.size(); i++)
    if (files.at(i) == path) {
      files.removeAt(i);
      if (fileType == Scene) m_recentSceneProjects.removeAt(i);
    }
  files.insert(0, path);
  if (fileType == Scene) m_recentSceneProjects.insert(0, projectName);
  int maxSize = 10;
  if (files.size() > maxSize) {
    files.removeAt(maxSize);
    if (fileType == Scene) m_recentSceneProjects.removeAt(maxSize);
  }

  if (fileType == Scene)
    m_recentScenes = files;
  else if (fileType == Level)
    m_recentLevels = files;
  else
    m_recentFlipbookImages = files;

  refreshRecentFilesMenu(fileType);
  saveRecentFiles();
}

//-----------------------------------------------------------------------------

void RecentFiles::moveFilePath(int fromIndex, int toIndex, FileType fileType) {
  if (fileType == Scene) {
    m_recentScenes.move(fromIndex, toIndex);
    m_recentSceneProjects.move(fromIndex, toIndex);
  } else if (fileType == Level)
    m_recentLevels.move(fromIndex, toIndex);
  else
    m_recentFlipbookImages.move(fromIndex, toIndex);
  saveRecentFiles();
}

//-----------------------------------------------------------------------------

void RecentFiles::removeFilePath(int index, FileType fileType) {
  if (fileType == Scene) {
    m_recentScenes.removeAt(index);
    m_recentSceneProjects.removeAt(index);
  } else if (fileType == Level)
    m_recentLevels.removeAt(index);
  saveRecentFiles();
}

//-----------------------------------------------------------------------------

QString RecentFiles::getFilePath(int index, FileType fileType) const {
  return (fileType == Scene)   ? m_recentScenes[index]
         : (fileType == Level) ? m_recentLevels[index]
                               : m_recentFlipbookImages[index];
}

//-----------------------------------------------------------------------------

QString RecentFiles::getFileProject(int index) const {
  if (index >= m_recentScenes.size() || index >= m_recentSceneProjects.size())
    return "-";
  return m_recentSceneProjects[index];
}

QString RecentFiles::getFileProject(QString fileName) const {
  for (int index = 0; index < m_recentScenes.size(); index++)
    if (m_recentScenes[index] == fileName) return m_recentSceneProjects[index];

  return "-";
}

//-----------------------------------------------------------------------------

void RecentFiles::clearRecentFilesList(FileType fileType) {
  if (fileType == Scene) {
    m_recentScenes.clear();
    m_recentSceneProjects.clear();
  } else if (fileType == Level)
    m_recentLevels.clear();
  else
    m_recentFlipbookImages.clear();

  refreshRecentFilesMenu(fileType);
  saveRecentFiles();
}

//-----------------------------------------------------------------------------

void RecentFiles::loadRecentFiles() {
  TFilePath fp = ToonzFolder::getMyModuleDir() + TFilePath("RecentFiles.ini");
  QSettings settings(toQString(fp), QSettings::IniFormat);
  int i;

  QList<QVariant> scenes = settings.value(QString("Scenes")).toList();
  if (!scenes.isEmpty()) {
    for (i = 0; i < scenes.size(); i++)
      m_recentScenes.append(scenes.at(i).toString());
  } else {
    QString scene = settings.value(QString("Scenes")).toString();
    if (!scene.isEmpty()) m_recentScenes.append(scene);
  }

  // Load scene's projects info. This is for display purposes only. For
  // backwards compatibility it is stored and maintained separately.
  QList<QVariant> sceneProjects =
      settings.value(QString("SceneProjects")).toList();
  if (!sceneProjects.isEmpty()) {
    for (i = 0; i < sceneProjects.size(); i++)
      m_recentSceneProjects.append(sceneProjects.at(i).toString());
  } else {
    QString sceneProject = settings.value(QString("SceneProjects")).toString();
    if (!sceneProject.isEmpty()) m_recentSceneProjects.append(sceneProject);
  }
  // Should be 1-to-1. If we're short, append projects list with "-".
  while (m_recentSceneProjects.size() < m_recentScenes.size())
    m_recentSceneProjects.append("-");

  QList<QVariant> levels = settings.value(QString("Levels")).toList();
  if (!levels.isEmpty()) {
    for (i = 0; i < levels.size(); i++) {
      QString path = levels.at(i).toString();
#ifdef x64
      if (path.endsWith(".mov") || path.endsWith(".3gp") ||
          path.endsWith(".pct") || path.endsWith(".pict"))
        continue;
#endif
      m_recentLevels.append(path);
    }
  } else {
    QString level = settings.value(QString("Levels")).toString();
    if (!level.isEmpty()) m_recentLevels.append(level);
  }

  QList<QVariant> flipImages =
      settings.value(QString("FlipbookImages")).toList();
  if (!flipImages.isEmpty()) {
    for (i = 0; i < flipImages.size(); i++)
      m_recentFlipbookImages.append(flipImages.at(i).toString());
  } else {
    QString flipImage = settings.value(QString("FlipbookImages")).toString();
    if (!flipImage.isEmpty()) m_recentFlipbookImages.append(flipImage);
  }

  refreshRecentFilesMenu(Scene);
  refreshRecentFilesMenu(Level);
  refreshRecentFilesMenu(Flip);
}

//-----------------------------------------------------------------------------

void RecentFiles::saveRecentFiles() {
  TFilePath fp = ToonzFolder::getMyModuleDir() + TFilePath("RecentFiles.ini");
  QSettings settings(toQString(fp), QSettings::IniFormat);
  settings.setValue(QString("Scenes"), QVariant(m_recentScenes));
  settings.setValue(QString("SceneProjects"), QVariant(m_recentSceneProjects));
  settings.setValue(QString("Levels"), QVariant(m_recentLevels));
  settings.setValue(QString("FlipbookImages"),
                    QVariant(m_recentFlipbookImages));
}

//-----------------------------------------------------------------------------

QList<QString> RecentFiles::getFilesNameList(FileType fileType) {
  QList<QString> files = (fileType == Scene)   ? m_recentScenes
                         : (fileType == Level) ? m_recentLevels
                                               : m_recentFlipbookImages;
  QList<QString> names;
  int i;
  for (i = 0; i < files.size(); i++) {
    TFilePath path(files.at(i).toStdWString());
    QString str, number;
    names.append(number.number(i + 1) + QString(". ") +
                 str.fromStdWString(path.getWideString()));
  }
  return names;
}

//-----------------------------------------------------------------------------

void RecentFiles::refreshRecentFilesMenu(FileType fileType) {
  CommandId id = (fileType == Scene)   ? MI_OpenRecentScene
                 : (fileType == Level) ? MI_OpenRecentLevel
                                       : MI_LoadRecentImage;
  QAction *act = CommandManager::instance()->getAction(id);
  if (!act) return;
  DVMenuAction *menu = dynamic_cast<DVMenuAction *>(act->menu());
  if (!menu) return;
  QList<QString> names = getFilesNameList(fileType);
  if (names.isEmpty())
    menu->setEnabled(false);
  else {
    CommandId clearActionId = (fileType == Scene)   ? MI_ClearRecentScene
                              : (fileType == Level) ? MI_ClearRecentLevel
                                                    : MI_ClearRecentImage;
    menu->setActions(names);
    menu->addSeparator();
    QAction *clearAction = CommandManager::instance()->getAction(clearActionId);
    assert(clearAction);
    menu->addAction(clearAction);
    if (!menu->isEnabled()) menu->setEnabled(true);
  }
}
