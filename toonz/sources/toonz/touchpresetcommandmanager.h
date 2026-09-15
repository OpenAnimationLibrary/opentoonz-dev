#pragma once

#ifndef TOUCH_PRESET_COMMAND_MANAGER_H
#define TOUCH_PRESET_COMMAND_MANAGER_H

#include <QActionGroup>
#include <QSet>
#include <QString>

class TouchPresetCommandManager {
  TouchPresetCommandManager();

  QSet<QString> m_registeredIds;
  QActionGroup *m_actionGroup = nullptr;

public:
  static TouchPresetCommandManager *instance();

  static QString commandIdForPreset(const QString &presetName);
  static QString presetNameFromCommandId(const QString &commandId);

  void registerTouchPresetCommands();
  void updateCheckedStates();
};

#endif
