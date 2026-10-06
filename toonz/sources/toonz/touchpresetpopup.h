#pragma once

#ifndef TOUCH_PRESET_POPUP_H
#define TOUCH_PRESET_POPUP_H

#include "touchslots.h"
#include "toonzqt/dvdialog.h"

class QComboBox;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QLabel;
class QLineEdit;
class QAction;
class ShortcutTree;

class TouchPresetPopup final : public DVGui::Dialog {
  Q_OBJECT

  QComboBox *m_presetCombo;
  QTreeWidget *m_tree;
  ShortcutTree *m_commands;
  QAction *m_chosenAction;
  QLineEdit *m_searchEdit;
  QLabel *m_noSearchLabel;
  QPushButton *m_newBtn;
  QPushButton *m_dupBtn;
  QPushButton *m_renameBtn;
  QPushButton *m_deleteBtn;
  QPushButton *m_resetBtn;
  QPushButton *m_assignBtn;
  QPushButton *m_clearBtn;
  QPushButton *m_useBtn;
  QLabel *m_selectedName;
  QLabel *m_selectedCommand;
  TouchSlot m_selectedSlot;

public:
  TouchPresetPopup();

private:
  void rebuildPresetCombo();
  void rebuildTree();
  void setSelectedSlot(TouchSlot slot);
  void refreshSelectionBar();
  QString currentPreset() const;
  void assignCurrent(const QString &commandId);

private slots:
  void onPresetChanged(int);
  void onUsePreset();
  void onNewPreset();
  void onDuplicatePreset();
  void onRenamePreset();
  void onDeletePreset();
  void onResetPreset();
  void onAssign();
  void onClear();
  void onSearchTextChanged(const QString &text);
  void onCommandSelected(QAction *action);
};

#endif
