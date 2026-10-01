#pragma once

#include "floatingpanelcommand.h"
#include "pane.h"
#include "tapp.h"
#include "tvectorimage.h"

#include "tools/strokeselection.h"

#include "toonz/toonzscene.h"
#include "toonz/tframehandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"

#include "toonzqt/menubarcommand.h"
#include "toonzqt/tselectionhandle.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVector>
#include <QWidgetList>

#include <algorithm>
#include <cassert>

namespace ExperimentalNamedGroups {

constexpr const char *kCommandId = "MI_OpenExperimentalNamedGroups";
constexpr const char *kPanelType = "ExperimentalNamedGroups";
constexpr const char *kSchemaName = "opentoonz.named-groups";
constexpr int kSchemaVersion = 1;
constexpr const char *kSidecarSuffix = ".namedgroups.json";

//=============================================================================
// NamedGroupsMetadataStore
//-----------------------------------------------------------------------------
//
// The PLI is authoritative for geometry, stroke order and grouping. This
// store owns only optional artist-facing metadata. Group locator semantics
// are deliberately not part of schema version 1 yet; the experiment first
// needs to establish which OpenToonz group identities survive save/load and
// ordinary group editing.

class NamedGroupsMetadataStore final {
public:
  enum class State { Unbound, Missing, Loaded, Invalid, Unsupported };

private:
  QString m_pliPath;
  QString m_sidecarPath;
  QJsonObject m_document;
  State m_state = State::Unbound;
  QString m_error;

public:
  void clear() {
    m_pliPath.clear();
    m_sidecarPath.clear();
    m_document = QJsonObject();
    m_state = State::Unbound;
    m_error.clear();
  }

  bool bind(const QString &physicalPliPath) {
    clear();
    if (physicalPliPath.isEmpty()) return false;

    m_pliPath = QFileInfo(physicalPliPath).absoluteFilePath();
    m_sidecarPath = m_pliPath + QString::fromLatin1(kSidecarSuffix);
    return load();
  }

  bool load() {
    m_error.clear();
    if (m_pliPath.isEmpty()) {
      clear();
      return false;
    }

    QFile file(m_sidecarPath);
    if (!file.exists()) {
      resetToSkeleton();
      m_state = State::Missing;
      return true;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      m_document = QJsonObject();
      m_state = State::Invalid;
      m_error = QObject::tr("Unable to read metadata: %1").arg(file.errorString());
      return false;
    }

    QJsonParseError parseError;
    const QJsonDocument parsed =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
      m_document = QJsonObject();
      m_state = State::Invalid;
      m_error = QObject::tr("Invalid Named Groups JSON: %1")
                    .arg(parseError.errorString());
      return false;
    }

    const QJsonObject root = parsed.object();
    const QString schema = root.value(QStringLiteral("schema")).toString();
    const int version = root.value(QStringLiteral("version")).toInt(-1);

    if (schema != QString::fromLatin1(kSchemaName) ||
        version != kSchemaVersion) {
      m_document = QJsonObject();
      m_state = State::Unsupported;
      m_error = QObject::tr(
          "This sidecar uses an unsupported Named Groups schema or version.");
      return false;
    }

    if (!root.value(QStringLiteral("level")).isString() ||
        !root.value(QStringLiteral("frames")).isObject()) {
      m_document = QJsonObject();
      m_state = State::Invalid;
      m_error = QObject::tr(
          "The Named Groups sidecar is missing required level/frames data.");
      return false;
    }

    const QString storedLevel = root.value(QStringLiteral("level")).toString();
    const QString currentLevel = QFileInfo(m_pliPath).fileName();
    if (storedLevel != currentLevel) {
      m_document = QJsonObject();
      m_state = State::Invalid;
      m_error = QObject::tr(
          "The Named Groups sidecar belongs to '%1', not '%2'.")
                    .arg(storedLevel, currentLevel);
      return false;
    }

    m_document = root;
    m_state = State::Loaded;
    return true;
  }

  bool save(QString *error = nullptr) {
    if (error) error->clear();

    if (m_pliPath.isEmpty()) {
      const QString message = QObject::tr("No PLI level is bound.");
      if (error) *error = message;
      m_error = message;
      return false;
    }

    // Never replace a file we could not understand. The user can repair or
    // move it and use Reload before attempting another save.
    if (m_state == State::Invalid || m_state == State::Unsupported) {
      const QString message = QObject::tr(
          "The existing sidecar is invalid or unsupported and was not overwritten.");
      if (error) *error = message;
      m_error = message;
      return false;
    }

    if (m_document.isEmpty()) resetToSkeleton();
    m_document.insert(QStringLiteral("schema"),
                      QString::fromLatin1(kSchemaName));
    m_document.insert(QStringLiteral("version"), kSchemaVersion);
    m_document.insert(QStringLiteral("level"),
                      QFileInfo(m_pliPath).fileName());
    if (!m_document.value(QStringLiteral("frames")).isObject())
      m_document.insert(QStringLiteral("frames"), QJsonObject());

    QSaveFile file(m_sidecarPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      const QString message =
          QObject::tr("Unable to write metadata: %1").arg(file.errorString());
      if (error) *error = message;
      m_error = message;
      return false;
    }

    const QByteArray bytes =
        QJsonDocument(m_document).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
      const QString message =
          QObject::tr("Unable to write all Named Groups metadata.");
      file.cancelWriting();
      if (error) *error = message;
      m_error = message;
      return false;
    }

    if (!file.commit()) {
      const QString message =
          QObject::tr("Unable to commit metadata: %1").arg(file.errorString());
      if (error) *error = message;
      m_error = message;
      return false;
    }

    m_state = State::Loaded;
    m_error.clear();
    return true;
  }

  bool isBound() const { return !m_pliPath.isEmpty(); }
  QString pliPath() const { return m_pliPath; }
  QString sidecarPath() const { return m_sidecarPath; }
  const QJsonObject &document() const { return m_document; }
  State state() const { return m_state; }
  QString errorString() const { return m_error; }

private:
  void resetToSkeleton() {
    m_document = QJsonObject{
        {QStringLiteral("schema"), QString::fromLatin1(kSchemaName)},
        {QStringLiteral("version"), kSchemaVersion},
        {QStringLiteral("level"), QFileInfo(m_pliPath).fileName()},
        {QStringLiteral("frames"), QJsonObject()}};
  }
};

//=============================================================================
// NamedGroupsPanel
//-----------------------------------------------------------------------------

class NamedGroupsPanel final : public TPanel {
  NamedGroupsMetadataStore m_store;
  QTreeWidget *m_tree = nullptr;
  QTreeWidgetItem *m_rootItem = nullptr;
  QLabel *m_pliPathLabel = nullptr;
  QLabel *m_sidecarPathLabel = nullptr;
  QLabel *m_statusLabel = nullptr;
  QPushButton *m_reloadButton = nullptr;
  QPushButton *m_saveButton = nullptr;
  int m_groupCount = 0;

  struct ActiveGroup {
    QTreeWidgetItem *item = nullptr;
    int serial = 0;
    int depth = 0;
    int firstStroke = -1;
    int lastStroke = -1;
  };

public:
  explicit NamedGroupsPanel(QWidget *parent = nullptr) : TPanel(parent) {
    setPanelType(kPanelType);
    setWindowTitle(QObject::tr("Experimental Named Groups"));
    setIsMaximizable(true);
    allowMultipleInstances(false);
    resize(760, 480);

    QWidget *body = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(body);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    QHBoxLayout *controls = new QHBoxLayout();
    m_reloadButton = new QPushButton(QObject::tr("Reload Metadata"), body);
    m_saveButton = new QPushButton(QObject::tr("Save Metadata"), body);
    controls->addWidget(m_reloadButton);
    controls->addWidget(m_saveButton);
    controls->addStretch(1);
    mainLayout->addLayout(controls);

    m_pliPathLabel = new QLabel(body);
    m_pliPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_sidecarPathLabel = new QLabel(body);
    m_sidecarPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mainLayout->addWidget(m_pliPathLabel);
    mainLayout->addWidget(m_sidecarPathLabel);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, body);

    m_tree = new QTreeWidget(splitter);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_rootItem = new QTreeWidgetItem(m_tree, QStringList(QObject::tr("Root")));
    m_rootItem->setExpanded(true);

    QWidget *schematicHost = new QWidget(splitter);
    QVBoxLayout *schematicLayout = new QVBoxLayout(schematicHost);
    schematicLayout->setContentsMargins(8, 8, 8, 8);
    QLabel *schematicTitle =
        new QLabel(QObject::tr("Named Group Schematic"), schematicHost);
    schematicTitle->setAlignment(Qt::AlignHCenter);
    QLabel *schematicPlaceholder = new QLabel(
        QObject::tr("The hierarchy at left is reconstructed directly from\n"
                    "the current TVectorImage without entering or modifying groups.\n\n"
                    "Selecting a currently reachable group row now mirrors the\n"
                    "native Vector Selection tool selection."),
        schematicHost);
    schematicPlaceholder->setAlignment(Qt::AlignCenter);
    schematicPlaceholder->setWordWrap(true);
    schematicLayout->addWidget(schematicTitle);
    schematicLayout->addWidget(schematicPlaceholder, 1);

    splitter->addWidget(m_tree);
    splitter->addWidget(schematicHost);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter, 1);

    m_statusLabel = new QLabel(body);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mainLayout->addWidget(m_statusLabel);

    setWidget(body);

    connect(m_reloadButton, &QPushButton::clicked, this, [this]() {
      if (m_store.isBound()) m_store.load();
      updateUi();
    });
    connect(m_saveButton, &QPushButton::clicked, this, [this]() {
      QString error;
      if (!m_store.save(&error) && !error.isEmpty())
        m_statusLabel->setText(error);
      else
        updateUi();
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            [this]() { syncTreeSelectionToDrawing(); });

    TXshLevelHandle *levelHandle = TApp::instance()->getCurrentLevel();
    connect(levelHandle, &TXshLevelHandle::xshLevelSwitched, this,
            [this](TXshLevel *) { bindCurrentPli(); });
    connect(levelHandle, &TXshLevelHandle::xshLevelChanged, this,
            [this]() { bindCurrentPli(); });
    connect(TApp::instance()->getCurrentScene(), &TSceneHandle::sceneSwitched,
            this, [this]() { bindCurrentPli(); });
    connect(TApp::instance()->getCurrentFrame(), &TFrameHandle::frameSwitched,
            this, [this]() { updateUi(); });

    QTimer::singleShot(0, this, [this]() { bindCurrentPli(); });
  }

private:
  TVectorImageP currentVectorImage() const {
    TApp *app = TApp::instance();
    TXshSimpleLevel *level = app->getCurrentLevel()->getSimpleLevel();
    TFrameHandle *frameHandle = app->getCurrentFrame();
    if (!level || level->getType() != PLI_XSHLEVEL || !frameHandle)
      return TVectorImageP();

    const TFrameId fid = frameHandle->getFid();
    TImageP frameImage = level->getFrame(fid, false);
    return TVectorImageP(frameImage);
  }

  void clearGroupTree() {
    QSignalBlocker blocker(m_tree);
    while (m_rootItem->childCount() > 0)
      delete m_rootItem->takeChild(0);
    m_groupCount = 0;
  }

  void updateGroupItem(ActiveGroup &group) {
    if (!group.item) return;
    group.item->setText(
        0, QObject::tr("Group %1 — depth %2 — strokes %3–%4")
               .arg(group.serial)
               .arg(group.depth)
               .arg(group.firstStroke + 1)
               .arg(group.lastStroke + 1));
    group.item->setData(0, Qt::UserRole, group.firstStroke);
    group.item->setData(0, Qt::UserRole + 1, group.lastStroke);
    group.item->setData(0, Qt::UserRole + 2, group.depth);
  }

  void populateGroupTree() {
    QSignalBlocker blocker(m_tree);

    while (m_rootItem->childCount() > 0)
      delete m_rootItem->takeChild(0);
    m_groupCount = 0;

    TVectorImageP image = currentVectorImage();
    if (!image) return;

    QVector<ActiveGroup> active;
    int nextSerial = 1;
    const int strokeCount = static_cast<int>(image->getStrokeCount());

    for (int stroke = 0; stroke < strokeCount; ++stroke) {
      const int depth = image->getGroupDepth(stroke);
      int commonDepth = 0;
      if (stroke > 0)
        commonDepth = image->getCommonGroupDepth(stroke - 1, stroke);

      commonDepth = std::max(0, commonDepth);
      commonDepth = std::min(commonDepth, depth);
      commonDepth = std::min(commonDepth, active.size());

      while (active.size() > commonDepth) active.removeLast();

      while (active.size() < depth) {
        const int groupDepth = active.size() + 1;
        QTreeWidgetItem *parentItem =
            active.isEmpty() ? m_rootItem : active.last().item;
        QTreeWidgetItem *item = new QTreeWidgetItem(parentItem);
        item->setExpanded(true);

        ActiveGroup group;
        group.item = item;
        group.serial = nextSerial++;
        group.depth = groupDepth;
        group.firstStroke = stroke;
        group.lastStroke = stroke;
        active.push_back(group);
        ++m_groupCount;
      }

      for (int i = 0; i < active.size(); ++i) {
        active[i].lastStroke = stroke;
        updateGroupItem(active[i]);
      }
    }

    if (m_groupCount == 0) {
      QTreeWidgetItem *empty = new QTreeWidgetItem(
          m_rootItem,
          QStringList(QObject::tr("(No vector groups in current frame)")));
      empty->setFlags(empty->flags() & ~Qt::ItemIsSelectable);
    }

    m_rootItem->setExpanded(true);
  }

  void syncTreeSelectionToDrawing() {
    const QList<QTreeWidgetItem *> selectedItems = m_tree->selectedItems();
    if (selectedItems.size() != 1) return;

    QTreeWidgetItem *item = selectedItems.front();
    if (!item || item == m_rootItem) return;

    bool firstOk = false;
    bool depthOk = false;
    const int firstStroke = item->data(0, Qt::UserRole).toInt(&firstOk);
    const int groupDepth = item->data(0, Qt::UserRole + 2).toInt(&depthOk);
    if (!firstOk || !depthOk) return;

    TVectorImageP image = currentVectorImage();
    if (!image || firstStroke < 0 ||
        firstStroke >= static_cast<int>(image->getStrokeCount()))
      return;

    TApp *app = TApp::instance();
    TSelectionHandle *selectionHandle = app->getCurrentSelection();
    StrokeSelection *strokeSelection = selectionHandle
        ? dynamic_cast<StrokeSelection *>(selectionHandle->getSelection())
        : nullptr;

    // StrokeSelection belongs to the Vector Selection tool. Do not create a
    // parallel selection object here: without the tool's View it would not
    // maintain bboxes, command state, or viewer invalidation correctly.
    if (!strokeSelection) {
      m_statusLabel->setText(QObject::tr(
          "Activate the Vector Selection tool to synchronize a Named Groups "
          "row with the drawing."));
      return;
    }

    const int enteredDepth = image->isInsideGroup();
    const int selectableDepth = enteredDepth + 1;

    // Native VectorSelectionTool::selectStroke() only selects a group that is
    // reachable at the current entered-group depth. Preserve that invariant
    // instead of silently entering/exiting groups or injecting an otherwise
    // unreachable nested selection.
    if (groupDepth != selectableDepth ||
        !image->isEnteredGroupStroke(firstStroke) ||
        !image->selectable(firstStroke)) {
      m_statusLabel->setText(
          QObject::tr("This group is at depth %1, but the drawing currently "
                      "allows direct selection at depth %2. Enter or exit "
                      "groups in the Viewer first, then select this row again.")
              .arg(groupDepth)
              .arg(selectableDepth));
      return;
    }

    if (strokeSelection->getImage() != image) {
      strokeSelection->selectNone();
      strokeSelection->setImage(image);
    } else {
      strokeSelection->selectNone();
    }

    int selectedStrokeCount = 0;
    const int strokeCount = static_cast<int>(image->getStrokeCount());
    for (int stroke = 0; stroke < strokeCount; ++stroke) {
      if (image->selectable(stroke) &&
          image->sameSubGroup(firstStroke, stroke)) {
        strokeSelection->select(stroke, true);
        ++selectedStrokeCount;
      }
    }

    if (selectedStrokeCount == 0) {
      m_statusLabel->setText(QObject::tr(
          "The selected Named Groups row did not resolve to a selectable "
          "vector group in the current drawing state."));
      return;
    }

    // notifyView() runs the Vector Selection tool's normal selection-change
    // path (bbox / invalidation). The handle notification keeps the rest of
    // OpenToonz synchronized with the same current selection object.
    strokeSelection->notifyView();
    selectionHandle->notifySelectionChanged();

    m_statusLabel->setText(
        QObject::tr("Selected %1 stroke(s) through the native vector group "
                    "selection path.")
            .arg(selectedStrokeCount));
  }

  void bindCurrentPli() {
    TApp *app = TApp::instance();
    TXshSimpleLevel *level = app->getCurrentLevel()->getSimpleLevel();
    ToonzScene *scene = app->getCurrentScene()->getScene();

    if (!level || level->getType() != PLI_XSHLEVEL || !scene) {
      m_store.clear();
      updateUi();
      return;
    }

    const TFilePath decodedPath = scene->decodeFilePath(level->getPath());
    const QString physicalPath = decodedPath.getQString();
    if (physicalPath.isEmpty()) {
      m_store.clear();
      updateUi();
      return;
    }

    const QString normalized = QFileInfo(physicalPath).absoluteFilePath();
    if (normalized != m_store.pliPath()) m_store.bind(normalized);
    updateUi();
  }

  void updateRootLabel() {
    QString rootText = QObject::tr("Root");
    if (m_store.isBound()) {
      TFrameHandle *frameHandle = TApp::instance()->getCurrentFrame();
      if (frameHandle) {
        const QString frameName =
            frameHandle->getFrameIndexName(frameHandle->getFrameIndex());
        if (!frameName.isEmpty())
          rootText = QObject::tr("Root — Frame %1").arg(frameName);
      }
    }
    m_rootItem->setText(0, rootText);
  }

  QString topologySummary() const {
    return QObject::tr(" %1 vector group(s) observed in the current frame.")
        .arg(m_groupCount);
  }

  void updateUi() {
    updateRootLabel();
    populateGroupTree();

    if (!m_store.isBound()) {
      m_pliPathLabel->setText(QObject::tr("PLI: No current PLI level"));
      m_sidecarPathLabel->setText(QObject::tr("Metadata: Not bound"));
      m_statusLabel->setText(QObject::tr(
          "Select a Toonz Vector (PLI) level to inspect its group hierarchy."));
      m_reloadButton->setEnabled(false);
      m_saveButton->setEnabled(false);
      return;
    }

    m_pliPathLabel->setText(
        QObject::tr("PLI: %1")
            .arg(QDir::toNativeSeparators(m_store.pliPath())));
    m_sidecarPathLabel->setText(
        QObject::tr("Metadata: %1")
            .arg(QDir::toNativeSeparators(m_store.sidecarPath())));
    m_reloadButton->setEnabled(true);

    switch (m_store.state()) {
    case NamedGroupsMetadataStore::State::Missing:
      m_statusLabel->setText(
          QObject::tr("No sidecar exists. Save Metadata will create the "
                      "adjacent JSON file.") +
          topologySummary());
      m_saveButton->setEnabled(true);
      break;
    case NamedGroupsMetadataStore::State::Loaded:
      m_statusLabel->setText(
          QObject::tr("Named Groups metadata is linked to the current PLI "
                      "level.") +
          topologySummary());
      m_saveButton->setEnabled(true);
      break;
    case NamedGroupsMetadataStore::State::Invalid:
    case NamedGroupsMetadataStore::State::Unsupported:
      m_statusLabel->setText(
          m_store.errorString() +
          QObject::tr(" The existing file is protected from overwrite.") +
          topologySummary());
      m_saveButton->setEnabled(false);
      break;
    case NamedGroupsMetadataStore::State::Unbound:
    default:
      m_statusLabel->setText(
          QObject::tr("Named Groups metadata is not bound.") +
          topologySummary());
      m_saveButton->setEnabled(false);
      break;
    }
  }
};

//=============================================================================
// Panel factory and command activation
//-----------------------------------------------------------------------------

class NamedGroupsPanelFactory final : public TPanelFactory {
public:
  NamedGroupsPanelFactory() : TPanelFactory(kPanelType) {}

  TPanel *createPanel(QWidget *parent) override {
    NamedGroupsPanel *panel = new NamedGroupsPanel(parent);
    panel->setObjectName(getPanelType());
    return panel;
  }

  void initialize(TPanel *) override { assert(false); }
};

static NamedGroupsPanelFactory namedGroupsPanelFactory;

inline QString normalizedMenuTitle(QString title) {
  title.remove(QLatin1Char('&'));
  return title.simplified().toCaseFolded();
}

inline bool isWindowsMenu(const QMenu *menu) {
  if (!menu) return false;
  const QString title = normalizedMenuTitle(menu->title());
  return title == normalizedMenuTitle(QStringLiteral("Windows")) ||
         title == normalizedMenuTitle(
                      QCoreApplication::translate("StackedMenuBar", "Windows")) ||
         title == normalizedMenuTitle(
                      QCoreApplication::translate("MainWindow", "Windows"));
}

class NamedGroupsUiActivator final : public QObject {
  QAction *m_action = nullptr;
  bool m_scanPending = false;

public:
  NamedGroupsUiActivator(QAction *action, QObject *parent)
      : QObject(parent), m_action(action) {}

  void start() {
    QApplication *application = qobject_cast<QApplication *>(qApp);
    if (!application || !m_action) return;
    application->installEventFilter(this);
    scheduleScan();
    QTimer::singleShot(1000, this, [this]() { scanUi(); });
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event && (event->type() == QEvent::Show ||
                  event->type() == QEvent::ShowToParent ||
                  event->type() == QEvent::Polish ||
                  event->type() == QEvent::ChildAdded)) {
      if (QMenu *menu = qobject_cast<QMenu *>(watched)) ensureMenu(menu);
      scheduleScan();
    }
    return QObject::eventFilter(watched, event);
  }

private:
  void ensureMenu(QMenu *menu) {
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

    const QWidgetList topLevels = application->topLevelWidgets();
    for (QWidget *topLevel : topLevels) {
      const QList<QMenu *> menus = topLevel->findChildren<QMenu *>();
      for (QMenu *menu : menus) ensureMenu(menu);
      if (QMenu *menu = qobject_cast<QMenu *>(topLevel)) ensureMenu(menu);
    }
  }
};

inline void registerNamedGroupsCommand() {
  if (CommandManager::instance()->getAction(kCommandId, false)) return;

  QAction *action =
      new DVAction(QObject::tr("Experimental Named Groups"), qApp);
  CommandManager::instance()->define(kCommandId, MenuWindowsCommandType, "",
                                     action, "");

  static OpenFloatingPanel *openCommand = new OpenFloatingPanel(
      kCommandId, kPanelType, QObject::tr("Experimental Named Groups"));
  Q_UNUSED(openCommand);

  QApplication *application = qobject_cast<QApplication *>(qApp);
  static NamedGroupsUiActivator *activator = nullptr;
  if (application && !activator) {
    activator = new NamedGroupsUiActivator(action, application);
    activator->start();
  }
}

}  // namespace ExperimentalNamedGroups

inline void registerExperimentalNamedGroupsStartup() {
  // Q_COREAPP_STARTUP_FUNCTION runs while QApplication construction is still
  // in progress. CommandManager::getAction() can create auxiliary QActions,
  // which is too early here. Queue registration until the first event-loop
  // turn so the QApplication and normal OpenToonz command infrastructure are
  // fully initialized before this experimental command touches them.
  if (!qApp) return;
  QTimer::singleShot(0, qApp, []() {
    ExperimentalNamedGroups::registerNamedGroupsCommand();
  });
}

Q_COREAPP_STARTUP_FUNCTION(registerExperimentalNamedGroupsStartup)
