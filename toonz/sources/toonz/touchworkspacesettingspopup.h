#pragma once

#ifndef TOUCH_WORKSPACE_SETTINGS_POPUP_H
#define TOUCH_WORKSPACE_SETTINGS_POPUP_H

#include "toonzqt/dvdialog.h"

#include <QListWidgetItem>

class QShowEvent;
class QSlider;
class QListWidget;
class QRadioButton;
class QCheckBox;
class QComboBox;

class TouchWorkspaceSettingsPopup final : public DVGui::Dialog {
  Q_OBJECT

  QRadioButton *m_layoutHorizontal;
  QRadioButton *m_layoutVertical;
  QRadioButton *m_layoutWheel;
  QRadioButton *m_styleClassic;
  QRadioButton *m_styleGhost;
  QComboBox *m_themeCombo;
  QSlider *m_sizeSlider;
  QSlider *m_opacitySlider;
  QListWidget *m_pinnedList;
  QCheckBox *m_deferTapCheck;
  bool m_livePickerPreview = false;

public:
  explicit TouchWorkspaceSettingsPopup(QWidget *parent = nullptr);
  ~TouchWorkspaceSettingsPopup() override;

  static void showPanel(QWidget *parent = nullptr);
  static bool isPanelOpen();
  static bool isLivePickerPreviewActive();
  static void syncDeferTapUiFromPreferences();

protected:
  void showEvent(QShowEvent *event) override;

private:
  void loadFromSettings();
  void rebuildPinnedList();

private slots:
  void onLayoutChanged(bool checked);
  void onStyleChanged(bool checked);
  void onThemeChanged(int index);
  void onSizeChanged(int value);
  void onOpacityChanged(int value);
  void onDeferTapChanged(bool checked);
  void onOpenPresetManager();
  void onPinnedItemChanged(QListWidgetItem *item);
};

#endif
