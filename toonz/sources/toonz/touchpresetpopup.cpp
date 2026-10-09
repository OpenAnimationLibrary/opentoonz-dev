#include "touchpresetpopup.h"

#include "touchworkspacesettingspopup.h"
#include "menubarcommandids.h"
#include "shortcutpopup.h"
#include "tapp.h"
#include "tenv.h"
#include "touchpresetmanager.h"

#include "toonz/preferences.h"
#include "toonz/preferencesitemids.h"
#include "toonz/toonzfolders.h"
#include "toonzqt/dvdialog.h"
#include "toonzqt/gutil.h"
#include "toonzqt/menubarcommand.h"
#include "tsystem.h"

#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QSizePolicy>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QAbstractButton>
#include <QPushButton>
#include <QRegularExpression>
#include <QMenu>
#include <QPointer>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidget>
#include <QHideEvent>
#include <QShowEvent>
#include <QVBoxLayout>

#include <string>

namespace {

// Persist gesture tree UI like Configure Shortcuts (.env).
TEnv::StringVar TouchPresetPopupExpandedFolders("TouchPresetPopupExpandedFolders",
                                                  "");
TEnv::IntVar TouchPresetPopupExpandedStateSaved(
    "TouchPresetPopupExpandedStateSaved", 0);

const QChar kGestureFolderListSeparator(0x1e);

QStringList expandedGestureFoldersFromEnv() {
  const QString stored =
      QString::fromStdString((std::string)TouchPresetPopupExpandedFolders);
  if (stored.isEmpty()) return QStringList();
  return stored.split(kGestureFolderListSeparator, Qt::SkipEmptyParts);
}

void saveExpandedGestureFoldersToEnv(const QStringList &expandedFolders) {
  TouchPresetPopupExpandedFolders =
      expandedFolders.join(kGestureFolderListSeparator).toStdString();
}


QPointer<TouchPresetPopup> s_touchPresetPopup;

QString stripAccel(QString text) {
  text.replace(QRegularExpression("&([^& ])"), "\\1");
  text.replace("&&", "&");
  return text;
}

void avoidIconFocusStuck(QAbstractButton *btn) {
  if (!btn) return;
  btn->setFocusPolicy(Qt::NoFocus);
}

QString commandLabel(const QString &id) {
  if (id.isEmpty()) return QObject::tr("(none)");
  if (QAction *a =
          CommandManager::instance()->getAction(id.toStdString().c_str())) {
    const QString label =
        a->iconText().isEmpty() ? a->text() : a->iconText();
    return stripAccel(label);
  }
  return id;
}

const int kSlotItemRole   = Qt::UserRole;
const int kFamilyItemRole = Qt::UserRole + 1;

QColor familyRecognitionStatusColor(bool recognized) {
  return recognized ? QColor(0x3d, 0xa5, 0x4a) : QColor(0xd4, 0x4a, 0x4a);
}

constexpr int kTouchPresetDefaultWidth  = 1000;
constexpr int kTouchPresetDefaultHeight = 640;
constexpr int kTouchPresetMinimumWidth  = 720;
constexpr int kTouchPresetMinimumHeight = 440;

bool touchPresetDialogHasSavedGeometry() {
  TFilePath savePath =
      ToonzFolder::getMyModuleDir() + TFilePath("popups.ini");
  QSettings settings(QString::fromStdWString(savePath.getWideString()),
                     QSettings::IniFormat);
  const QString geo =
      settings.value(QStringLiteral("TouchPresetDialogGeometry")).toString();
  if (geo.isEmpty()) return false;
  return geo.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() == 4;
}

}  // namespace

//-----------------------------------------------------------------------------

TouchPresetPopup::TouchPresetPopup()
    : Dialog(TApp::instance()->getMainWindow(), false, false, "TouchPreset")
    , m_chosenAction(nullptr)
    , m_selectedSlot(TouchSlot_None) {
  setWindowTitle(tr("Touch Preset Manager"));

  m_presetCombo = new QComboBox(this);
  m_useBtn = new QPushButton(tr("Use"), this);
  m_useBtn->setIcon(createQIcon("touch_preset_use"));
  avoidIconFocusStuck(m_useBtn);
  m_useBtn->setContextMenuPolicy(Qt::CustomContextMenu);
  m_autoUsePresetAction =
      new QAction(tr("Auto Use on selection"), this);
  m_autoUsePresetAction->setCheckable(true);
  connect(m_autoUsePresetAction, &QAction::toggled, this,
          &TouchPresetPopup::onAutoUsePresetToggled);
  connect(m_useBtn, &QWidget::customContextMenuRequested, this,
          &TouchPresetPopup::onUseButtonContextMenu);
  refreshUseButton();
  m_newBtn = new QPushButton(tr("New"), this);
  m_newBtn->setToolTip(tr("Create a new touch preset"));
  m_newBtn->setIcon(createQIcon("touch_preset_new"));
  avoidIconFocusStuck(m_newBtn);
  m_dupBtn = new QPushButton(tr("Duplicate"), this);
  m_dupBtn->setToolTip(tr("Duplicate the current touch preset"));
  m_dupBtn->setIcon(createQIcon("touch_preset_duplicate"));
  avoidIconFocusStuck(m_dupBtn);
  m_renameBtn = new QPushButton(tr("Rename"), this);
  m_renameBtn->setToolTip(tr("Rename the current touch preset"));
  m_renameBtn->setIcon(createQIcon("touch_preset_rename"));
  avoidIconFocusStuck(m_renameBtn);
  m_deleteBtn = new QPushButton(tr("Delete"), this);
  m_deleteBtn->setToolTip(tr("Delete Current Preset"));
  m_deleteBtn->setIcon(createQIcon("touch_preset_delete"));
  avoidIconFocusStuck(m_deleteBtn);
  m_resetBtn = new QPushButton(tr("Reset"), this);
  m_resetBtn->setToolTip(tr("Reset the current touch preset to defaults"));
  m_resetBtn->setIcon(createQIcon("touch_preset_reset"));
  avoidIconFocusStuck(m_resetBtn);
  m_loadPresetBtn = new QPushButton(tr("Load"), this);
  m_loadPresetBtn->setToolTip(tr("Load a touch preset from file"));
  m_loadPresetBtn->setIcon(createQIcon("touch_preset_load"));
  avoidIconFocusStuck(m_loadPresetBtn);
  m_savePresetBtn = new QPushButton(tr("Save As"), this);
  m_savePresetBtn->setToolTip(tr("Save current touch preset as a new preset"));
  m_savePresetBtn->setIcon(createQIcon("touch_preset_saveas"));
  avoidIconFocusStuck(m_savePresetBtn);
  m_exportPresetBtn = new QPushButton(tr("Export Current Touch Preset"), this);
  m_exportPresetBtn->setToolTip(tr("Export Current Touch Preset"));
  m_exportPresetBtn->setIcon(createQIcon("touch_preset_export"));
  avoidIconFocusStuck(m_exportPresetBtn);

  m_gestureSearchEdit = new QLineEdit(this);
  m_commandSearchEdit = new QLineEdit(this);
  m_optionsBtn        = new QToolButton(this);
  m_optionsMenu       = new QMenu(this);
  m_optionsBtn->setIcon(createQIcon("menu"));
  m_optionsBtn->setToolTip(tr("Options"));
  m_optionsBtn->setPopupMode(QToolButton::InstantPopup);
  m_optionsBtn->setAutoRaise(true);
  m_optionsBtn->setFocusPolicy(Qt::NoFocus);
  buildOptionsMenu();
  m_optionsBtn->setMenu(m_optionsMenu);
  m_noSearchLabel =
      new QLabel(tr("Couldn't find any matching command."), this);
  m_noSearchLabel->setHidden(true);

  m_tree = new QTreeWidget(this);
  m_tree->setObjectName("ShortcutTree");
  m_tree->setColumnCount(2);
  m_tree->setHeaderLabels(QStringList() << tr("Gesture") << tr("Command"));
  m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_tree->setRootIsDecorated(true);
  m_tree->setAlternatingRowColors(true);
  m_tree->setIndentation(14);
  m_tree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  m_tree->setMinimumSize(0, 0);
  m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

  m_commands = new ShortcutTree(this);
  m_commands->setMinimumWidth(320);
  m_commands->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_commands->setMinimumHeight(0);

  m_selectedName    = new QLabel(tr("No gesture selected"), this);
  m_selectedCommand = new QLabel(tr("(none)"), this);
  m_assignBtn = new QPushButton(tr("Assign"), this);
  m_assignBtn->setToolTip(tr("Assign the selected command to the gesture"));
  m_clearBtn = new QPushButton(tr("Clear"), this);
  m_clearBtn->setToolTip(tr("Clear the command assigned to the gesture"));

  m_topLayout->setContentsMargins(5, 5, 5, 5);
  m_topLayout->setSpacing(8);

  QHBoxLayout *bodyLay = new QHBoxLayout();
  bodyLay->setContentsMargins(0, 0, 0, 0);
  bodyLay->setSpacing(8);

  QVBoxLayout *gestureLay = new QVBoxLayout();
  gestureLay->setContentsMargins(0, 0, 0, 0);
  gestureLay->setSpacing(4);
  QHBoxLayout *gestureSearchLay = new QHBoxLayout();
  gestureSearchLay->setContentsMargins(0, 0, 0, 0);
  gestureSearchLay->setSpacing(5);
  gestureSearchLay->addWidget(new QLabel(tr("Search gestures:"), this), 0);
  gestureSearchLay->addWidget(m_gestureSearchEdit, 1);
  gestureSearchLay->addWidget(m_optionsBtn, 0);
  gestureLay->addLayout(gestureSearchLay);
  gestureLay->addWidget(m_tree, 1);
  bodyLay->addLayout(gestureLay, 1);

  QVBoxLayout *cmdLay = new QVBoxLayout();
  cmdLay->setContentsMargins(0, 0, 0, 0);
  cmdLay->setSpacing(4);
  QHBoxLayout *commandSearchLay = new QHBoxLayout();
  commandSearchLay->setContentsMargins(0, 0, 0, 0);
  commandSearchLay->setSpacing(5);
  commandSearchLay->addWidget(new QLabel(tr("Search commands:"), this), 0);
  commandSearchLay->addWidget(m_commandSearchEdit, 1);
  cmdLay->addLayout(commandSearchLay);
  cmdLay->addWidget(m_noSearchLabel, 0, Qt::AlignTop | Qt::AlignHCenter);
  cmdLay->addWidget(m_commands, 1);
  bodyLay->addLayout(cmdLay, 1);
  m_topLayout->addLayout(bodyLay, 1);

  QHBoxLayout *editLay = new QHBoxLayout();
  editLay->setSpacing(6);
  editLay->addWidget(m_selectedName, 1);
  editLay->addWidget(m_selectedCommand, 1);
  editLay->addWidget(m_assignBtn);
  editLay->addWidget(m_clearBtn);
  m_topLayout->addLayout(editLay);

  QGroupBox *presetBox = new QGroupBox(tr("Touch Presets"), this);
  presetBox->setObjectName("SolidLineFrame");
  presetBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  QHBoxLayout *presetLay = new QHBoxLayout();
  presetLay->setContentsMargins(5, 5, 5, 5);
  presetLay->setSpacing(5);
  presetLay->addWidget(new QLabel(tr("Preset:"), this));
  presetLay->addWidget(m_presetCombo, 1);
  presetLay->addWidget(m_useBtn);
  presetLay->addWidget(m_newBtn);
  presetLay->addWidget(m_dupBtn);
  presetLay->addWidget(m_renameBtn);
  presetLay->addWidget(m_deleteBtn);
  presetLay->addWidget(m_resetBtn);
  presetLay->addWidget(m_loadPresetBtn);
  presetLay->addWidget(m_savePresetBtn);
  presetBox->setLayout(presetLay);
  m_topLayout->addWidget(presetBox);
  m_topLayout->addSpacing(10);
  QHBoxLayout *exportLay = new QHBoxLayout();
  exportLay->setContentsMargins(5, 0, 5, 0);
  exportLay->setSpacing(5);
  exportLay->addWidget(m_exportPresetBtn, 0);
  m_topLayout->addLayout(exportLay, 0);
  m_topLayout->setStretch(0, 1);
  for (int i = 1; i < m_topLayout->count(); ++i) m_topLayout->setStretch(i, 0);

  connect(m_presetCombo, SIGNAL(currentIndexChanged(int)), this,
          SLOT(onPresetChanged(int)));
  connect(m_useBtn, SIGNAL(clicked()), this, SLOT(onUsePreset()));
  connect(m_newBtn, SIGNAL(clicked()), this, SLOT(onNewPreset()));
  connect(m_dupBtn, SIGNAL(clicked()), this, SLOT(onDuplicatePreset()));
  connect(m_renameBtn, SIGNAL(clicked()), this, SLOT(onRenamePreset()));
  connect(m_deleteBtn, SIGNAL(clicked()), this, SLOT(onDeletePreset()));
  connect(m_resetBtn, SIGNAL(clicked()), this, SLOT(onResetPreset()));
  connect(m_loadPresetBtn, SIGNAL(clicked()), this, SLOT(onLoadPreset()));
  connect(m_savePresetBtn, SIGNAL(clicked()), this, SLOT(onSavePreset()));
  connect(m_exportPresetBtn, SIGNAL(clicked()), this, SLOT(onExportPreset()));
  connect(m_assignBtn, SIGNAL(clicked()), this, SLOT(onAssign()));
  connect(m_clearBtn, SIGNAL(clicked()), this, SLOT(onClear()));
  connect(m_gestureSearchEdit, &QLineEdit::textChanged, this,
          &TouchPresetPopup::onGestureSearchChanged);
  connect(m_commandSearchEdit, &QLineEdit::textChanged, this,
          &TouchPresetPopup::onCommandSearchChanged);
  connect(m_tree, &QTreeWidget::itemChanged, this,
          &TouchPresetPopup::onTreeItemChanged);
  connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
          &TouchPresetPopup::onGestureTreeContextMenu);
  connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *) {
    if (m_gestureSearchEdit->text().trimmed().isEmpty())
      saveGestureTreeExpandedState();
  });
  connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *) {
    if (m_gestureSearchEdit->text().trimmed().isEmpty())
      saveGestureTreeExpandedState();
  });
  connect(m_commands, &ShortcutTree::actionSelected, this,
          &TouchPresetPopup::onCommandSelected);
  connect(m_commands, &ShortcutTree::searched, m_noSearchLabel,
          &QLabel::setHidden);
  connect(m_commands, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *, int) {
            if (m_chosenAction) onAssign();
          });
  connect(m_tree, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
            if (!current || !current->parent()) return;
            setSelectedSlot((TouchSlot)current->data(0, Qt::UserRole).toInt());
          });
  connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item, int) {
            if (!item || !item->parent() || item->isDisabled()) return;
            if (m_chosenAction) onAssign();
          });
  connect(m_optionsMenu, &QMenu::aboutToShow, this,
          &TouchPresetPopup::syncOptionsMenu);
  TouchPresetManager::instance()->setActivePresetListener(
      this, [this](const QString &) {
        if (isVisible()) syncToActivePreset();
      });

  rebuildPresetCombo();
  rebuildTree();
  refreshSelectionBar();

  setMinimumSize(kTouchPresetMinimumWidth, kTouchPresetMinimumHeight);
  if (!touchPresetDialogHasSavedGeometry())
    resize(kTouchPresetDefaultWidth, kTouchPresetDefaultHeight);

  s_touchPresetPopup = this;
}

TouchPresetPopup::~TouchPresetPopup() {
  if (s_touchPresetPopup == this) s_touchPresetPopup = nullptr;
  TouchPresetManager::instance()->setActivePresetListener(nullptr, nullptr);
}

void TouchPresetPopup::syncDeferTapUiFromPreferences() {
  if (!s_touchPresetPopup || !s_touchPresetPopup->m_deferTapAction) return;
  QSignalBlocker block(s_touchPresetPopup->m_deferTapAction);
  s_touchPresetPopup->m_deferTapAction->setChecked(
      Preferences::instance()->isTouchDeferTapForDoubleTapEnabled());
}

void TouchPresetPopup::syncSuppressContextMenuUiFromPreferences() {
  if (!s_touchPresetPopup || !s_touchPresetPopup->m_suppressContextMenuAction)
    return;
  QSignalBlocker block(s_touchPresetPopup->m_suppressContextMenuAction);
  s_touchPresetPopup->m_suppressContextMenuAction->setChecked(
      Preferences::instance()->isTouchSuppressViewerContextMenuEnabled());
}

void TouchPresetPopup::showEvent(QShowEvent *event) {
  Dialog::showEvent(event);
  refreshUseButton();
  syncToActivePreset();
}

void TouchPresetPopup::hideEvent(QHideEvent *event) {
  saveGestureTreeExpandedState();
  Dialog::hideEvent(event);
}

QStringList TouchPresetPopup::collectGestureTreeExpandedState() const {
  QStringList expandedFolders;
  for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *folder = m_tree->topLevelItem(i);
    if (folder && folder->isExpanded())
      expandedFolders.append(folder->text(0));
  }
  return expandedFolders;
}

void TouchPresetPopup::applyGestureTreeExpandedState(
    const QStringList &expandedFolders, bool useDefaultIfEmpty) {
  QSignalBlocker blocker(m_tree);
  for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *folder = m_tree->topLevelItem(i);
    if (!folder) continue;
    const QString folderName = folder->text(0);
    if (!expandedFolders.isEmpty()) {
      folder->setExpanded(expandedFolders.contains(folderName));
    } else if (useDefaultIfEmpty) {
      folder->setExpanded(folder->text(0) !=
                          touchSlotFamilyLabel(TouchSlotFamily_Reserved));
    }
  }
}

void TouchPresetPopup::saveGestureTreeExpandedState() {
  if (m_tree->topLevelItemCount() == 0) return;
  saveExpandedGestureFoldersToEnv(collectGestureTreeExpandedState());
  TouchPresetPopupExpandedStateSaved = 1;
}

void TouchPresetPopup::restoreGestureTreeExpandedState() {
  const bool hasSavedState = TouchPresetPopupExpandedStateSaved != 0;
  applyGestureTreeExpandedState(expandedGestureFoldersFromEnv(), !hasSavedState);
}

void TouchPresetPopup::syncToActivePreset() {
  const QString active = TouchPresetManager::instance()->activePreset();
  const int idx        = m_presetCombo->findData(active);
  if (idx >= 0) {
    QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->setCurrentIndex(idx);
  }
  rebuildPresetCombo();
  rebuildTree();
  refreshSelectionBar();
}

void TouchPresetPopup::rebuildPresetCombo() {
  TouchPresetManager *mgr = TouchPresetManager::instance();
  const QString current =
      m_presetCombo->count() ? currentPreset() : mgr->activePreset();
  QSignalBlocker blocker(m_presetCombo);
  m_presetCombo->clear();
  const QStringList names = mgr->presetNames();
  for (const QString &name : names) {
    m_presetCombo->addItem(name);
    if (name == mgr->activePreset())
      m_presetCombo->setItemText(m_presetCombo->count() - 1,
                                 name + tr(" (active)"));
    m_presetCombo->setItemData(m_presetCombo->count() - 1, name);
  }
  int idx = names.indexOf(current);
  if (idx < 0) idx = names.indexOf(mgr->activePreset());
  if (idx >= 0) m_presetCombo->setCurrentIndex(idx);

  const QString editing = currentPreset();
  const QString active  = mgr->activePreset();
  if (!editing.isEmpty() && editing != active)
    m_presetCombo->setToolTip(
        tr("Editing \"%1\". Touch gestures use \"%2\" until you click Use.")
            .arg(editing, active));
  else
    m_presetCombo->setToolTip(
        tr("Selected preset is active for touch gestures."));
}

QString TouchPresetPopup::currentPreset() const {
  const QString name = m_presetCombo->currentData().toString();
  if (!name.isEmpty() &&
      TouchPresetManager::instance()->hasPreset(name))
    return name;
  return TouchPresetManager::instance()->activePreset();
}

void TouchPresetPopup::rebuildTree() {
  if (m_tree->topLevelItemCount() > 0) saveGestureTreeExpandedState();
  QSignalBlocker treeBlocker(m_tree);
  m_tree->clear();
  TouchPresetManager *mgr = TouchPresetManager::instance();
  const QString preset    = currentPreset();

  auto addRow = [&](QTreeWidgetItem *parent, TouchSlot slot, bool locked) {
    QTreeWidgetItem *item = new QTreeWidgetItem(parent);
    item->setText(0, touchSlotLabel(slot));
    item->setData(0, Qt::UserRole, (int)slot);
    if (locked) {
      item->setText(1, tr("Locked"));
      item->setDisabled(true);
    } else {
      item->setText(1, commandLabel(mgr->commandId(preset, slot)));
    }
  };

  QTreeWidgetItem *locked = new QTreeWidgetItem(m_tree);
  locked->setText(0, touchSlotFamilyLabel(TouchSlotFamily_Reserved));
  for (TouchSlot slot : reservedTouchSlots()) addRow(locked, slot, true);
  locked->setExpanded(false);

  for (TouchSlotFamily family : assignableTouchFamilies()) {
    QTreeWidgetItem *group = new QTreeWidgetItem(m_tree);
    group->setText(0, touchSlotFamilyLabel(family));
    group->setData(0, kFamilyItemRole, (int)family);
    applyFamilyRecognitionRow(group, family);
    const QList<TouchSlot> familySlots = touchSlotsInFamily(family);
    for (TouchSlot slot : familySlots) addRow(group, slot, false);
  }

  restoreGestureTreeExpandedState();
  applyGestureTreeFilter(m_gestureSearchEdit->text());
  setSelectedSlot(m_selectedSlot);
}

void TouchPresetPopup::applyFamilyRecognitionRow(QTreeWidgetItem *group,
                                                 TouchSlotFamily family) {
  TouchPresetManager *mgr = TouchPresetManager::instance();
  const bool show         = mgr->showFamilyRecognitionToggles();
  const bool enabled      = mgr->isFamilyRecognitionEnabled(family);
  Qt::ItemFlags flags     = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  if (show) {
    flags |= Qt::ItemIsUserCheckable;
    group->setCheckState(0, enabled ? Qt::Checked : Qt::Unchecked);
    group->setText(1, enabled ? tr("Recognized") : tr("Not recognized"));
    const QString tip =
        enabled ? tr("Use this gesture family in OpenToonz.")
                : tr("Ignore this gesture family in OpenToonz.");
    group->setToolTip(0, tip);
    group->setToolTip(1, tip);
    if (mgr->showFamilyRecognitionColors()) {
      const QBrush brush(familyRecognitionStatusColor(enabled));
      group->setForeground(1, brush);
    } else {
      group->setForeground(1, QBrush());
    }
  } else {
    group->setText(1, QString());
    group->setToolTip(0, QString());
    group->setToolTip(1, QString());
    group->setForeground(1, QBrush());
  }
  group->setFlags(flags);
}

void TouchPresetPopup::setSelectedSlot(TouchSlot slot) {
  if (touchSlotIsReserved(slot)) slot = TouchSlot_None;
  m_selectedSlot = slot;

  if (slot != TouchSlot_None) {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
      QTreeWidgetItem *folder = m_tree->topLevelItem(i);
      for (int j = 0; j < folder->childCount(); ++j) {
        QTreeWidgetItem *item = folder->child(j);
        if (item->data(0, Qt::UserRole).toInt() == (int)slot) {
          QSignalBlocker blocker(m_tree);
          m_tree->setCurrentItem(item);
          break;
        }
      }
    }
  }
  refreshSelectionBar();
}

void TouchPresetPopup::refreshSelectionBar() {
  const bool has = m_selectedSlot != TouchSlot_None &&
                   !touchSlotIsReserved(m_selectedSlot);
  if (!has) {
    m_selectedName->setText(tr("No gesture selected"));
    m_selectedCommand->setText(tr("(none)"));
    m_assignBtn->setEnabled(false);
    m_clearBtn->setEnabled(false);
    return;
  }
  m_selectedName->setText(touchSlotLabel(m_selectedSlot));
  m_selectedCommand->setText(commandLabel(
      TouchPresetManager::instance()->commandId(currentPreset(),
                                                m_selectedSlot)));
  m_assignBtn->setEnabled(m_chosenAction != nullptr);
  m_clearBtn->setEnabled(true);
}

void TouchPresetPopup::applyGestureTreeFilter(const QString &text) {
  const QString t = text.trimmed();
  for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *top = m_tree->topLevelItem(i);
    const bool familyMatch =
        t.isEmpty() || top->text(0).contains(t, Qt::CaseInsensitive);
    bool anyChildVisible = false;
    for (int j = 0; j < top->childCount(); ++j) {
      QTreeWidgetItem *child = top->child(j);
      const bool childMatch =
          t.isEmpty() || child->text(0).contains(t, Qt::CaseInsensitive);
      const bool visible = familyMatch || childMatch;
      child->setHidden(!visible);
      if (visible) anyChildVisible = true;
    }
    top->setHidden(!t.isEmpty() && !familyMatch && !anyChildVisible);
    if (!t.isEmpty() && !top->isHidden() && (familyMatch || anyChildVisible))
      top->setExpanded(true);
  }
}

void TouchPresetPopup::onGestureSearchChanged(const QString &text) {
  applyGestureTreeFilter(text);
  if (text.trimmed().isEmpty()) restoreGestureTreeExpandedState();
}

void TouchPresetPopup::onCommandSearchChanged(const QString &text) {
  static bool busy = false;
  if (busy) return;
  busy = true;
  m_commands->searchItems(text);
  busy = false;
}

void TouchPresetPopup::onCommandSelected(QAction *action) {
  m_chosenAction = action;
  refreshSelectionBar();
}

void TouchPresetPopup::onPresetChanged(int) {
  rebuildTree();
  applyActivePresetIfAutoUse();
}

void TouchPresetPopup::applyActivePresetIfAutoUse() {
  if (!TouchPresetManager::instance()->autoUsePresetOnSelection()) return;
  const QString name = currentPreset();
  if (name.isEmpty()) return;
  TouchPresetManager::instance()->setActivePreset(name);
  rebuildPresetCombo();
}

void TouchPresetPopup::onUsePreset() {
  const QString name = currentPreset();
  if (name.isEmpty()) return;
  TouchPresetManager::instance()->setActivePreset(name);
  rebuildPresetCombo();
}

void TouchPresetPopup::onUseButtonContextMenu(const QPoint &pos) {
  QMenu menu(this);
  menu.addAction(m_autoUsePresetAction);
  menu.exec(m_useBtn->mapToGlobal(pos));
}

void TouchPresetPopup::onAutoUsePresetToggled(bool checked) {
  TouchPresetManager::instance()->setAutoUsePresetOnSelection(checked);
  refreshUseButton();
  if (checked) applyActivePresetIfAutoUse();
}

void TouchPresetPopup::refreshUseButton() {
  const bool autoUse =
      TouchPresetManager::instance()->autoUsePresetOnSelection();
  QSignalBlocker block(m_autoUsePresetAction);
  m_autoUsePresetAction->setChecked(autoUse);
  if (autoUse) {
    m_useBtn->setToolTip(
        tr("Auto Use is on: the selected preset is active for touch. "
           "Right-click to change."));
    m_useBtn->setStyleSheet(QStringLiteral("font-weight: bold;"));
  } else {
    m_useBtn->setToolTip(
        tr("Use selected preset for touch gestures. Right-click for Auto Use."));
    m_useBtn->setStyleSheet(QString());
  }
}

void TouchPresetPopup::onNewPreset() {
  bool ok            = false;
  const QString name = QInputDialog::getText(
      this, tr("New Touch Preset"), tr("Name:"), QLineEdit::Normal, QString(),
      &ok);
  if (!ok || name.trimmed().isEmpty()) return;
  TouchPresetManager::instance()->createPreset(name.trimmed());
  rebuildPresetCombo();
  const int idx =
      TouchPresetManager::instance()->presetNames().indexOf(name.trimmed());
  if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
  rebuildTree();
}

void TouchPresetPopup::onDuplicatePreset() {
  const QString from = currentPreset();
  bool ok            = false;
  const QString name = QInputDialog::getText(
      this, tr("Duplicate Touch Preset"), tr("Name:"), QLineEdit::Normal,
      from + tr(" copy"), &ok);
  if (!ok || name.trimmed().isEmpty()) return;
  const QString trimmed = name.trimmed();
  TouchPresetManager::instance()->duplicatePreset(from, trimmed);
  rebuildPresetCombo();
  const int idx =
      TouchPresetManager::instance()->presetNames().indexOf(trimmed);
  if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
  rebuildTree();
}

void TouchPresetPopup::onRenamePreset() {
  const QString from = currentPreset();
  bool ok            = false;
  const QString name = QInputDialog::getText(
      this, tr("Rename Touch Preset"), tr("Name:"), QLineEdit::Normal, from,
      &ok);
  if (!ok || name.trimmed().isEmpty()) return;
  if (TouchPresetManager::instance()->renamePreset(from, name.trimmed()))
    rebuildPresetCombo();
}

void TouchPresetPopup::onDeletePreset() {
  TouchPresetManager::instance()->removePreset(currentPreset());
  m_selectedSlot = TouchSlot_None;
  rebuildPresetCombo();
  rebuildTree();
}

void TouchPresetPopup::onResetPreset() {
  TouchPresetManager::instance()->resetPreset(currentPreset());
  rebuildTree();
}

void TouchPresetPopup::onLoadPreset() {
  const QString qs = QFileDialog::getOpenFileName(
      this, tr("Load Touch Preset File"), QString(),
      tr("Touch preset files (*.ini);;All files (*.*)"));
  if (qs.isEmpty()) return;
  const TFilePath path(qs.toStdWString());
  if (!TFileStatus(path).doesExist()) return;

  TouchPresetManager *mgr = TouchPresetManager::instance();
  QSettings probe(toQString(path), QSettings::IniFormat);
  QString presetName = probe.value(QStringLiteral("General/preset")).toString();
  if (presetName.isEmpty())
    presetName = path.withoutParentDir().getQString();
  presetName = QFileInfo(presetName).completeBaseName();

  bool replace = false;
  if (mgr->hasPreset(presetName)) {
    const QString question =
        tr("Replace the existing preset \"%1\" with this file?").arg(presetName);
    const int ret =
        DVGui::MsgBox(question, QObject::tr("Yes"), QObject::tr("No"), 0);
    if (ret == 0 || ret == 2) return;
    replace = true;
  }

  const QString imported =
      mgr->importPresetFromFile(toQString(path), replace);
  if (imported.isEmpty()) {
    DVGui::MsgBox(DVGui::CRITICAL,
                  tr("Could not load touch preset from this file."));
    return;
  }
  mgr->setPinnedForPicker(imported, true);
  m_selectedSlot = TouchSlot_None;
  rebuildPresetCombo();
  const int importIdx = mgr->presetNames().indexOf(imported);
  if (importIdx >= 0) {
    QSignalBlocker comboBlock(m_presetCombo);
    m_presetCombo->setCurrentIndex(importIdx);
  }
  rebuildTree();
}

void TouchPresetPopup::onSavePreset() {
  TouchPresetManager *mgr = TouchPresetManager::instance();
  const QString from     = currentPreset();
  QString name =
      DVGui::getText(tr("Enter Preset Name"), tr("Preset Name:"), from);
  if (name.isEmpty()) return;

  if (mgr->hasPreset(name) && name != from) {
    const QString question =
        tr("Replace the existing preset \"%1\"?").arg(name);
    const int ret =
        DVGui::MsgBox(question, QObject::tr("Yes"), QObject::tr("No"), 0);
    if (ret == 0 || ret == 2) return;
    for (TouchSlot slot : assignableTouchSlots())
      mgr->setCommandId(name, slot, mgr->commandId(from, slot));
  } else if (!mgr->hasPreset(name)) {
    mgr->duplicatePreset(from, name);
  }

  const TFilePath dir =
      ToonzFolder::getMyModuleDir() + TFilePath("touchpresetspresets");
  if (!TSystem::doesExistFileOrLevel(dir)) TSystem::mkDir(dir);
  const TFilePath fp = dir + TFilePath(name.toStdString() + ".ini");
  if (!mgr->exportPresetToFile(name, toQString(fp))) {
    DVGui::MsgBox(DVGui::CRITICAL, tr("Could not save touch preset file."));
    return;
  }
  rebuildPresetCombo();
  const int idx = mgr->presetNames().indexOf(name);
  if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
}

void TouchPresetPopup::onExportPreset() {
  TouchPresetManager *mgr = TouchPresetManager::instance();
  const QString preset  = currentPreset();
  QString qs = QFileDialog::getSaveFileName(
      this, tr("Export Current Touch Preset"), preset + QStringLiteral(".ini"),
      tr("Touch preset files (*.ini);;All files (*.*)"));
  if (qs.isEmpty()) return;
  if (!qs.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive))
    qs += QStringLiteral(".ini");
  if (!mgr->exportPresetToFile(preset, qs)) {
    DVGui::MsgBox(DVGui::CRITICAL, tr("Could not export touch preset file."));
  }
}

void TouchPresetPopup::assignCurrent(const QString &commandId) {
  if (m_selectedSlot == TouchSlot_None || touchSlotIsReserved(m_selectedSlot))
    return;
  TouchPresetManager::instance()->setCommandId(currentPreset(), m_selectedSlot,
                                               commandId);
  const QString label = commandLabel(commandId);
  for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *folder = m_tree->topLevelItem(i);
    for (int j = 0; j < folder->childCount(); ++j) {
      QTreeWidgetItem *item = folder->child(j);
      if (item->data(0, Qt::UserRole).toInt() == (int)m_selectedSlot)
        item->setText(1, label);
    }
  }
  refreshSelectionBar();
}

void TouchPresetPopup::onAssign() {
  if (m_selectedSlot == TouchSlot_None || touchSlotIsReserved(m_selectedSlot) ||
      !m_chosenAction)
    return;
  assignCurrent(QString::fromStdString(
      CommandManager::instance()->getIdFromAction(m_chosenAction)));
}

void TouchPresetPopup::onClear() { assignCurrent(QString()); }

void TouchPresetPopup::buildOptionsMenu() {
  m_optionsMenu->clear();
  QAction *workspaceSettings = m_optionsMenu->addAction(
      tr("Touch Workspace Settings..."));
  connect(workspaceSettings, &QAction::triggered, this, []() {
    TouchWorkspaceSettingsPopup::showPanel();
  });
  m_optionsMenu->addSeparator();

  m_deferTapAction = m_optionsMenu->addAction(
      tr("Defer simple tap when a double tap is assigned"));
  m_deferTapAction->setCheckable(true);
  connect(m_deferTapAction, &QAction::toggled, this,
          &TouchPresetPopup::onDeferTapToggled);

  m_suppressContextMenuAction =
      m_optionsMenu->addAction(tr("Hide context menu on touch hold"));
  m_suppressContextMenuAction->setCheckable(true);
  connect(m_suppressContextMenuAction, &QAction::toggled, this,
          &TouchPresetPopup::onSuppressContextMenuToggled);

  m_optionsMenu->addSeparator();

  m_showFamilyTogglesAction = m_optionsMenu->addAction(
      tr("Show checkboxes to disable gesture families"));
  m_showFamilyTogglesAction->setCheckable(true);
  connect(m_showFamilyTogglesAction, &QAction::toggled, this,
          &TouchPresetPopup::onShowFamilyTogglesToggled);
  m_showFamilyColorsAction = m_optionsMenu->addAction(
      tr("Show recognized status in color"));
  m_showFamilyColorsAction->setCheckable(true);
  connect(m_showFamilyColorsAction, &QAction::toggled, this,
          &TouchPresetPopup::onShowFamilyColorsToggled);
  m_showFamilyColorsAction->setEnabled(false);
}

void TouchPresetPopup::syncOptionsMenu() {
  if (m_deferTapAction) {
    QSignalBlocker deferBlock(m_deferTapAction);
    m_deferTapAction->setChecked(
        Preferences::instance()->isTouchDeferTapForDoubleTapEnabled());
  }
  if (m_suppressContextMenuAction) {
    QSignalBlocker contextBlock(m_suppressContextMenuAction);
    m_suppressContextMenuAction->setChecked(
        Preferences::instance()->isTouchSuppressViewerContextMenuEnabled());
  }
  QSignalBlocker togglesBlock(m_showFamilyTogglesAction);
  m_showFamilyTogglesAction->setChecked(
      TouchPresetManager::instance()->showFamilyRecognitionToggles());
  QSignalBlocker colorsBlock(m_showFamilyColorsAction);
  m_showFamilyColorsAction->setChecked(
      TouchPresetManager::instance()->showFamilyRecognitionColors());
  m_showFamilyColorsAction->setEnabled(
      TouchPresetManager::instance()->showFamilyRecognitionToggles());
}

void TouchPresetPopup::onDeferTapToggled(bool checked) {
  Preferences::instance()->setValue(touchDeferTapForDoubleTap, checked);
  TouchWorkspaceSettingsPopup::syncDeferTapUiFromPreferences();
}

void TouchPresetPopup::onSuppressContextMenuToggled(bool checked) {
  Preferences::instance()->setValue(touchSuppressViewerContextMenu, checked);
  TouchWorkspaceSettingsPopup::syncSuppressContextMenuUiFromPreferences();
}

void TouchPresetPopup::onShowFamilyTogglesToggled(bool checked) {
  TouchPresetManager::instance()->setShowFamilyRecognitionToggles(checked);
  if (m_showFamilyColorsAction) m_showFamilyColorsAction->setEnabled(checked);
  rebuildTree();
}

void TouchPresetPopup::onShowFamilyColorsToggled(bool checked) {
  TouchPresetManager::instance()->setShowFamilyRecognitionColors(checked);
  rebuildTree();
}

void TouchPresetPopup::onGestureTreeContextMenu(const QPoint &pos) {
  QTreeWidgetItem *item = m_tree->itemAt(pos);
  if (!item || !item->data(0, kFamilyItemRole).isValid()) return;

  syncOptionsMenu();
  QMenu menu(this);
  menu.addAction(m_showFamilyTogglesAction);
  menu.addAction(m_showFamilyColorsAction);
  menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

//-----------------------------------------------------------------------------

void TouchPresetPopup::onTreeItemChanged(QTreeWidgetItem *item, int column) {
  if (!item || column != 0) return;
  if (!item->data(0, kFamilyItemRole).isValid()) return;
  const TouchSlotFamily family =
      (TouchSlotFamily)item->data(0, kFamilyItemRole).toInt();
  const bool enabled = item->checkState(0) == Qt::Checked;
  TouchPresetManager *mgr = TouchPresetManager::instance();
  if (mgr->isFamilyRecognitionEnabled(family) == enabled) return;
  mgr->setFamilyRecognitionEnabled(family, enabled);
  QSignalBlocker blocker(m_tree);
  applyFamilyRecognitionRow(item, family);
}

OpenPopupCommandHandler<TouchPresetPopup> openTouchPresetPopup(
    MI_TouchPresetManager);
