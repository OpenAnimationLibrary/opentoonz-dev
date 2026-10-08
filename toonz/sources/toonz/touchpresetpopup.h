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
class QShowEvent;
class QHideEvent;

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
  QPushButton *m_exportPresetBtn;
  QPushButton *m_assignBtn;
  QPushButton *m_clearBtn;
  QPushButton *m_useBtn;
  QToolButton *m_optionsBtn;
  QMenu *m_optionsMenu;
  QAction *m_showFamilyTogglesAction;
  QAction *m_showFamilyColorsAction;
  QAction *m_deferTapAction;
  QAction *m_suppressContextMenuAction;
  QAction *m_autoUsePresetAction;
  QLabel *m_selectedName;
  QLabel *m_selectedCommand;
  TouchSlot m_selectedSlot;

public:
  TouchPresetPopup();
  ~TouchPresetPopup();

  static void syncDeferTapUiFromPreferences();
  static void syncSuppressContextMenuUiFromPreferences();

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
  void syncToActivePreset();
  void refreshUseButton();
  void applyActivePresetIfAutoUse();
  QStringList collectGestureTreeExpandedState() const;
  void applyGestureTreeExpandedState(const QStringList &expandedFolders,
                                     bool useDefaultIfEmpty);
  void saveGestureTreeExpandedState();
  void restoreGestureTreeExpandedState();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private slots:
  void onShowFamilyTogglesToggled(bool checked);
  void onShowFamilyColorsToggled(bool checked);
  void onDeferTapToggled(bool checked);
  void onSuppressContextMenuToggled(bool checked);
  void syncOptionsMenu();
  void onTreeItemChanged(QTreeWidgetItem *item, int column);
  void onGestureTreeContextMenu(const QPoint &pos);
  void onPresetChanged(int);
  void onUsePreset();
  void onUseButtonContextMenu(const QPoint &pos);
  void onAutoUsePresetToggled(bool checked);
  void onNewPreset();
  void onDuplicatePreset();
  void onRenamePreset();
  void onDeletePreset();
  void onResetPreset();
  void onLoadPreset();
  void onSavePreset();
  void onExportPreset();
  void onAssign();
  void onClear();
  void onGestureSearchChanged(const QString &text);
  void onCommandSearchChanged(const QString &text);
  void onCommandSelected(QAction *action);
};

#endif
