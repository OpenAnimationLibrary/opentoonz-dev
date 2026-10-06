#include "touchpresetpopup.h"

#include "menubarcommandids.h"
#include "shortcutpopup.h"
#include "tapp.h"
#include "touchpresetmanager.h"

#include "toonzqt/menubarcommand.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QString stripAccel(QString text) {
  text.replace(QRegularExpression("&([^& ])"), "\\1");
  text.replace("&&", "&");
  return text;
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

}  // namespace

//-----------------------------------------------------------------------------

TouchPresetPopup::TouchPresetPopup()
    : Dialog(TApp::instance()->getMainWindow(), false, false, "TouchPreset")
    , m_chosenAction(nullptr)
    , m_selectedSlot(TouchSlot_None) {
  setWindowTitle(tr("Touch Preset Manager"));

  m_presetCombo = new QComboBox(this);
  m_useBtn      = new QPushButton(tr("Use"), this);
  m_newBtn      = new QPushButton(tr("New"), this);
  m_dupBtn      = new QPushButton(tr("Duplicate"), this);
  m_renameBtn   = new QPushButton(tr("Rename"), this);
  m_deleteBtn   = new QPushButton(tr("Delete"), this);
  m_resetBtn    = new QPushButton(tr("Reset"), this);

  m_searchEdit = new QLineEdit(this);
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
  m_tree->setMinimumSize(480, 360);

  m_commands = new ShortcutTree(this);
  m_commands->setMinimumWidth(320);

  m_selectedName    = new QLabel(tr("No gesture selected"), this);
  m_selectedCommand = new QLabel(tr("(none)"), this);
  m_assignBtn       = new QPushButton(tr("Assign"), this);
  m_clearBtn        = new QPushButton(tr("Clear"), this);

  m_topLayout->setContentsMargins(5, 5, 5, 5);
  m_topLayout->setSpacing(8);

  QHBoxLayout *searchLay = new QHBoxLayout();
  searchLay->setContentsMargins(0, 0, 0, 0);
  searchLay->setSpacing(5);
  searchLay->addWidget(new QLabel(tr("Search:"), this), 0);
  searchLay->addWidget(m_searchEdit, 1);
  m_topLayout->addLayout(searchLay, 0);

  QHBoxLayout *bodyLay = new QHBoxLayout();
  bodyLay->setContentsMargins(0, 0, 0, 0);
  bodyLay->setSpacing(8);
  bodyLay->addWidget(m_tree, 1);

  QVBoxLayout *cmdLay = new QVBoxLayout();
  cmdLay->setContentsMargins(0, 0, 0, 0);
  cmdLay->setSpacing(0);
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
  presetBox->setLayout(presetLay);
  m_topLayout->addWidget(presetBox);

  connect(m_presetCombo, SIGNAL(currentIndexChanged(int)), this,
          SLOT(onPresetChanged(int)));
  connect(m_useBtn, SIGNAL(clicked()), this, SLOT(onUsePreset()));
  connect(m_newBtn, SIGNAL(clicked()), this, SLOT(onNewPreset()));
  connect(m_dupBtn, SIGNAL(clicked()), this, SLOT(onDuplicatePreset()));
  connect(m_renameBtn, SIGNAL(clicked()), this, SLOT(onRenamePreset()));
  connect(m_deleteBtn, SIGNAL(clicked()), this, SLOT(onDeletePreset()));
  connect(m_resetBtn, SIGNAL(clicked()), this, SLOT(onResetPreset()));
  connect(m_assignBtn, SIGNAL(clicked()), this, SLOT(onAssign()));
  connect(m_clearBtn, SIGNAL(clicked()), this, SLOT(onClear()));
  connect(m_searchEdit, &QLineEdit::textChanged, this,
          &TouchPresetPopup::onSearchTextChanged);
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
}

QString TouchPresetPopup::currentPreset() const {
  return m_presetCombo->currentData().toString();
}

void TouchPresetPopup::rebuildTree() {
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
  locked->setText(0, tr("Reserved (locked for now)"));
  for (TouchSlot slot : reservedTouchSlots()) addRow(locked, slot, true);
  locked->setExpanded(false);

  for (TouchSlotFamily family : assignableTouchFamilies()) {
    QTreeWidgetItem *group = new QTreeWidgetItem(m_tree);
    group->setText(0, touchSlotFamilyLabel(family));
    const QList<TouchSlot> familySlots = touchSlotsInFamily(family);
    for (TouchSlot slot : familySlots) addRow(group, slot, false);
    group->setExpanded(true);
  }

  setSelectedSlot(m_selectedSlot);
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

void TouchPresetPopup::onSearchTextChanged(const QString &text) {
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

void TouchPresetPopup::onPresetChanged(int) { rebuildTree(); }

void TouchPresetPopup::onUsePreset() {
  const QString name = currentPreset();
  if (name.isEmpty()) return;
  TouchPresetManager::instance()->setActivePreset(name);
  rebuildPresetCombo();
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
  TouchPresetManager::instance()->duplicatePreset(from, name.trimmed());
  rebuildPresetCombo();
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
  rebuildPresetCombo();
  rebuildTree();
}

void TouchPresetPopup::onResetPreset() {
  TouchPresetManager::instance()->resetPreset(currentPreset());
  rebuildTree();
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

OpenPopupCommandHandler<TouchPresetPopup> openTouchPresetPopup(
    MI_TouchPresetManager);
