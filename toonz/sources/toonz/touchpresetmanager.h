#pragma once

#ifndef TOUCH_PRESET_MANAGER_H
#define TOUCH_PRESET_MANAGER_H

#include "touchslots.h"

#include <QMap>
#include <QString>
#include <QStringList>

class TouchPresetManager {
  QMap<QString, QMap<TouchSlot, QString>> m_presets;
  QString m_active;
  bool m_loaded = false;

  TouchPresetManager() {}

  void ensureLoaded();
  void seedFactory();
  QString settingsPath() const;

public:
  static TouchPresetManager *instance();

  QString activePreset() const;
  void setActivePreset(const QString &name);

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

  void save();
  void load();
};

#endif
