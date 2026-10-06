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
class QToolButton;
class QMenu;
class ShortcutTree;

class TouchPresetPopup final : public DVGui::Dialog {
  Q_OBJECT

  QComboBox *m_presetCombo;
  QTreeWidget *m_tree;
  ShortcutTree *m_commands;
  QAction *m_chosenAction;
  QLineEdit *m_gestureSearchEdit;
  QLineEdit *m_commandSearchEdit;
  QLabel *m_noSearchLabel;
  QPushButton *m_newBtn;
  QPushButton *m_dupBtn;
  QPushButton *m_renameBtn;
  QPushButton *m_deleteBtn;
  QPushButton *m_resetBtn;
  QPushButton *m_loadPresetBtn;
  QPushButton *m_savePresetBtn;
  QPushButton *m_assignBtn;
  QPushButton *m_clearBtn;
  QPushButton *m_useBtn;
  QToolButton *m_optionsBtn;
  QMenu *m_optionsMenu;
  QAction *m_showFamilyTogglesAction;
  QAction *m_deferTapAction;
  QLabel *m_selectedName;
  QLabel *m_selectedCommand;
  TouchSlot m_selectedSlot;

public:
  TouchPresetPopup();

private:
  void rebuildPresetCombo();
  void rebuildTree();
  void applyFamilyRecognitionRow(QTreeWidgetItem *group, TouchSlotFamily family);
  void applyGestureTreeFilter(const QString &text);
  void setSelectedSlot(TouchSlot slot);
  void refreshSelectionBar();
  QString currentPreset() const;
  void assignCurrent(const QString &commandId);
  void buildOptionsMenu();

private slots:
  void onShowFamilyTogglesToggled(bool checked);
  void onDeferTapToggled(bool checked);
  void syncOptionsMenu();
  void onTreeItemChanged(QTreeWidgetItem *item, int column);
  void onPresetChanged(int);
  void onUsePreset();
  void onNewPreset();
  void onDuplicatePreset();
  void onRenamePreset();
  void onDeletePreset();
  void onResetPreset();
  void onLoadPreset();
  void onSavePreset();
  void onAssign();
  void onClear();
  void onGestureSearchChanged(const QString &text);
  void onCommandSearchChanged(const QString &text);
  void onCommandSelected(QAction *action);
};

#endif
