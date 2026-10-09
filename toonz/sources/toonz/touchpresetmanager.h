#pragma once

#ifndef TOUCH_PRESET_MANAGER_H
#define TOUCH_PRESET_MANAGER_H

#include "touchslots.h"

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

enum class TouchPresetPickerLayout { Horizontal, Vertical, Wheel };

enum class TouchPresetPickerStyle { Classic, Ghost };

enum class TouchPresetPickerTheme {
  OpenToonz,
  HologramCyan,
  WhiteGreen,
  CreamSage,
};

enum class TouchViewerHudStyle { Gray, Frost };

class TouchPresetManager {
  QMap<QString, QMap<TouchSlot, QString>> m_presets;
  QMap<TouchSlotFamily, bool> m_familyRecognition;
  bool m_showFamilyRecognitionToggles = false;
  bool m_showFamilyRecognitionColors  = false;
  bool m_autoUsePresetOnSelection     = false;
  QString m_active;
  QStringList m_pinnedForPicker;
  TouchPresetPickerLayout m_pickerLayout = TouchPresetPickerLayout::Wheel;
  TouchPresetPickerStyle m_pickerStyle   = TouchPresetPickerStyle::Classic;
  int m_pickerSizePercent    = 150;
  int m_pickerOpacityPercent = 100;
  TouchPresetPickerTheme m_pickerTheme = TouchPresetPickerTheme::OpenToonz;
  bool m_pickerShowOnlyPinned = false;
  bool m_viewerHudEnabled = true;
  TouchViewerHudStyle m_viewerHudStyle = TouchViewerHudStyle::Frost;
  bool m_loaded = false;

  struct ActivePresetListener {
    QPointer<QObject> owner;
    std::function<void(const QString &)> callback;
  };
  ActivePresetListener m_activeListener;

  void notifyActivePresetChanged();

  void seedFamilyRecognition();

  TouchPresetManager() {}

  void ensureLoaded();
  void seedFactory();
  QString settingsPath() const;

public:
  static TouchPresetManager *instance();

  QString activePreset() const;
  void setActivePreset(const QString &name);
  void setActivePresetListener(
      QObject *owner, const std::function<void(const QString &)> &callback);

  QStringList presetNames() const;
  bool hasPreset(const QString &name) const;

  void createPreset(const QString &name);
  void duplicatePreset(const QString &from, const QString &to);
  bool renamePreset(const QString &from, const QString &to);
  bool removePreset(const QString &name);
  void resetPreset(const QString &name);

  QString commandId(TouchSlot slot) const;
  QString commandId(const QString &preset, TouchSlot slot) const;
  void setCommandId(const QString &preset, TouchSlot slot, const QString &id);
  bool hasCommand(TouchSlot slot) const;

  void execute(TouchSlot slot);

  bool isFamilyRecognitionEnabled(TouchSlotFamily family) const;
  void setFamilyRecognitionEnabled(TouchSlotFamily family, bool enabled);

  bool showFamilyRecognitionToggles() const;
  void setShowFamilyRecognitionToggles(bool show);

  bool showFamilyRecognitionColors() const;
  void setShowFamilyRecognitionColors(bool show);

  bool autoUsePresetOnSelection() const;
  void setAutoUsePresetOnSelection(bool on);

  bool exportPresetToFile(const QString &presetName,
                          const QString &filePath) const;
  QString importPresetFromFile(const QString &filePath, bool replaceIfExists);

  QStringList pinnedPresetsForPicker() const;
  bool isPinnedForPicker(const QString &name) const;
  bool setPinnedForPicker(const QString &name, bool pinned);
  static int maxPinnedPresetsForPicker();

  TouchPresetPickerLayout pickerLayout() const;
  void setPickerLayout(TouchPresetPickerLayout layout);

  TouchPresetPickerStyle pickerStyle() const;
  void setPickerStyle(TouchPresetPickerStyle style);

  int pickerSizePercent() const;
  void setPickerSizePercent(int percent);
  int pickerOpacityPercent() const;
  void setPickerOpacityPercent(int percent);

  TouchPresetPickerTheme pickerTheme() const;
  void setPickerTheme(TouchPresetPickerTheme theme);

  bool pickerShowOnlyPinned() const;
  void setPickerShowOnlyPinned(bool onlyPinned);

  bool viewerHudEnabled() const;
  void setViewerHudEnabled(bool on);

  TouchViewerHudStyle viewerHudStyle() const;
  void setViewerHudStyle(TouchViewerHudStyle style);

  void save();
  void load();
};

#endif
