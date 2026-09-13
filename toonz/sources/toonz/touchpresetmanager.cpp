#include "touchpresetmanager.h"

#include "tfilepath.h"
#include "toonz/toonzfolders.h"
#include "toonzqt/gutil.h"
#include "toonzqt/menubarcommand.h"

#include <QAction>
#include <QSettings>

namespace {

const char *kDefault = "Default";

QMap<TouchSlot, QString> factoryMap() {
  QMap<TouchSlot, QString> map;
  map[TouchSlot_Tap2]          = QStringLiteral("MI_Undo");
  map[TouchSlot_Tap3]          = QStringLiteral("MI_Redo");
  map[TouchSlot_Tap4]          = QStringLiteral("MI_CreateBlankDrawing");
  map[TouchSlot_Tap5]          = QStringLiteral("MI_Duplicate");
  map[TouchSlot_DoubleTap2]    = QStringLiteral("MI_Play");
  map[TouchSlot_DoubleTap3]    = QStringLiteral("MI_OnionSkin");
  map[TouchSlot_DoubleTap4]    = QStringLiteral("T_ZoomFit");
  map[TouchSlot_DoubleTap5]    = QStringLiteral("T_ViewReset");
  map[TouchSlot_Hold2]         = QStringLiteral("T_Brush");
  map[TouchSlot_Hold3]         = QStringLiteral("T_Eraser");
  map[TouchSlot_Hold4]         = QStringLiteral("T_Fill");
  map[TouchSlot_Hold5]         = QStringLiteral("T_StylePicker");
  map[TouchSlot_Swipe3Left]    = QStringLiteral("MI_Pause");
  map[TouchSlot_Swipe3Right]   = QStringLiteral("MI_Play");
  map[TouchSlot_Swipe3Up]      = QStringLiteral("MI_PrevDrawing");
  map[TouchSlot_Swipe3Down]    = QStringLiteral("MI_NextDrawing");
  map[TouchSlot_Swipe4Left]    = QStringLiteral("MI_PrevFrame");
  map[TouchSlot_Swipe4Right]   = QStringLiteral("MI_NextFrame");
  map[TouchSlot_Swipe4Up]      = QStringLiteral("MI_FirstFrame");
  map[TouchSlot_Swipe4Down]    = QStringLiteral("MI_LastFrame");
  map[TouchSlot_Swipe5Left]    = QStringLiteral("MI_PrevKeyframe");
  map[TouchSlot_Swipe5Right]   = QStringLiteral("MI_NextKeyframe");
  map[TouchSlot_Swipe5Up]      = QStringLiteral("MI_PrevStep");
  map[TouchSlot_Swipe5Down]    = QStringLiteral("MI_NextStep");
  map[TouchSlot_Pinch3In]      = QStringLiteral("T_Zoomout");
  map[TouchSlot_Pinch3Out]     = QStringLiteral("T_Zoomin");
  map[TouchSlot_Pinch4In]      = QStringLiteral("T_ActualPixelSize");
  map[TouchSlot_Pinch4Out]     = QStringLiteral("T_ZoomReset");
  map[TouchSlot_Edge3Left]     = QStringLiteral("MI_OpenPalette");
  map[TouchSlot_Edge3Right]    = QStringLiteral("MI_OpenXshView");
  map[TouchSlot_Edge3Top]      = QStringLiteral("MI_OpenToolOptionBar");
  map[TouchSlot_Edge3Bottom]   = QStringLiteral("MI_OpenFilmStrip");
  map[TouchSlot_StylusPlus1]   = QStringLiteral("T_Selection");
  map[TouchSlot_StylusPlus2]   = QStringLiteral("MI_ShiftTrace");
  return map;
}

const int kTouchPresetsFormat = 2;

QString familyRecognitionKey(TouchSlotFamily family) {
  switch (family) {
  case TouchSlotFamily_Tap:
    return QStringLiteral("Tap");
  case TouchSlotFamily_DoubleTap:
    return QStringLiteral("DoubleTap");
  case TouchSlotFamily_Hold:
    return QStringLiteral("Hold");
  case TouchSlotFamily_Swipe:
    return QStringLiteral("Swipe");
  case TouchSlotFamily_Pinch:
    return QStringLiteral("Pinch");
  case TouchSlotFamily_Edge:
    return QStringLiteral("Edge");
  case TouchSlotFamily_Stylus:
    return QStringLiteral("Stylus");
  default:
    return QString();
  }
}

bool migrateSwipeUndoRedo(QMap<QString, QMap<TouchSlot, QString>> &presets) {
  bool changed = false;
  for (auto it = presets.begin(); it != presets.end(); ++it) {
    QMap<TouchSlot, QString> &map = it.value();
    if (map.value(TouchSlot_Swipe3Left) == QLatin1String("MI_Undo")) {
      map[TouchSlot_Swipe3Left] = QStringLiteral("MI_Pause");
      changed                   = true;
    }
    if (map.value(TouchSlot_Swipe3Right) == QLatin1String("MI_Redo")) {
      map[TouchSlot_Swipe3Right] = QStringLiteral("MI_Play");
      changed                    = true;
    }
  }
  return changed;
}

}  // namespace

TouchPresetManager *TouchPresetManager::instance() {
  static TouchPresetManager mgr;
  mgr.ensureLoaded();
  return &mgr;
}

void TouchPresetManager::ensureLoaded() {
  if (!m_loaded) load();
}

void TouchPresetManager::seedFactory() {
  m_presets[QLatin1String(kDefault)] = factoryMap();
  m_active = QLatin1String(kDefault);
}

void TouchPresetManager::seedFamilyRecognition() {
  for (TouchSlotFamily family : assignableTouchFamilies())
    m_familyRecognition[family] = true;
}

QString TouchPresetManager::settingsPath() const {
  return toQString(ToonzFolder::getMyModuleDir() +
                   TFilePath("touchpresets.ini"));
}

void TouchPresetManager::load() {
  m_presets.clear();
  m_familyRecognition.clear();
  seedFactory();
  seedFamilyRecognition();
  QSettings s(settingsPath(), QSettings::IniFormat);
  const QString active =
      s.value(QStringLiteral("General/activePreset")).toString();
  s.beginGroup(QStringLiteral("Presets"));
  const QStringList groups = s.childGroups();
  for (const QString &name : groups) {
    if (name.isEmpty()) continue;
    s.beginGroup(name);
    QMap<TouchSlot, QString> map = m_presets.value(name);
    const QStringList keys       = s.childKeys();
    for (const QString &key : keys) {
      const TouchSlot slot = touchSlotFromId(key);
      if (slot == TouchSlot_None || touchSlotIsReserved(slot)) continue;
      map[slot] = s.value(key).toString();
    }
    s.endGroup();
    m_presets[name] = map;
  }
  s.endGroup();
  if (!active.isEmpty() && m_presets.contains(active)) m_active = active;

  s.beginGroup(QStringLiteral("Recognition"));
  for (TouchSlotFamily family : assignableTouchFamilies()) {
    const QString key = familyRecognitionKey(family);
    if (key.isEmpty()) continue;
    if (s.contains(key))
      m_familyRecognition[family] = s.value(key, true).toBool();
  }
  s.endGroup();

  m_showFamilyRecognitionToggles =
      s.value(QStringLiteral("General/showFamilyRecognitionToggles"), false)
          .toBool();

  const QStringList leftover = {QStringLiteral("Animation"),
                                QStringLiteral("Paint"),
                                QStringLiteral("Background")};
  bool dropped               = false;
  for (const QString &name : leftover) {
    if (m_presets.remove(name)) dropped = true;
  }
  if (!m_presets.contains(QLatin1String(kDefault)))
    m_presets[QLatin1String(kDefault)] = factoryMap();
  if (m_active.isEmpty() || !m_presets.contains(m_active))
    m_active = QLatin1String(kDefault);

  const int format = s.value(QStringLiteral("General/format"), 1).toInt();
  bool persist     = dropped;
  if (format < kTouchPresetsFormat) {
    migrateSwipeUndoRedo(m_presets);
    persist = true;
  }

  m_loaded = true;
  if (persist) save();
}

void TouchPresetManager::save() {
  QSettings s(settingsPath(), QSettings::IniFormat);
  s.clear();
  s.setValue(QStringLiteral("General/format"), kTouchPresetsFormat);
  s.setValue(QStringLiteral("General/activePreset"), m_active);
  s.setValue(QStringLiteral("General/showFamilyRecognitionToggles"),
             m_showFamilyRecognitionToggles);
  s.beginGroup(QStringLiteral("Presets"));
  for (auto it = m_presets.constBegin(); it != m_presets.constEnd(); ++it) {
    s.beginGroup(it.key());
    for (auto slotIt = it.value().constBegin(); slotIt != it.value().constEnd();
         ++slotIt) {
      if (touchSlotIsReserved(slotIt.key())) continue;
      if (!slotIt.value().isEmpty())
        s.setValue(QLatin1String(touchSlotId(slotIt.key())), slotIt.value());
    }
    s.endGroup();
  }
  s.endGroup();
  s.beginGroup(QStringLiteral("Recognition"));
  for (TouchSlotFamily family : assignableTouchFamilies()) {
    const QString key = familyRecognitionKey(family);
    if (key.isEmpty()) continue;
    s.setValue(key, m_familyRecognition.value(family, true));
  }
  s.endGroup();
}

bool TouchPresetManager::isFamilyRecognitionEnabled(
    TouchSlotFamily family) const {
  return m_familyRecognition.value(family, true);
}

void TouchPresetManager::setFamilyRecognitionEnabled(TouchSlotFamily family,
                                                     bool enabled) {
  if (family == TouchSlotFamily_Reserved) return;
  m_familyRecognition[family] = enabled;
  save();
}

bool TouchPresetManager::showFamilyRecognitionToggles() const {
  return m_showFamilyRecognitionToggles;
}

void TouchPresetManager::setShowFamilyRecognitionToggles(bool show) {
  m_showFamilyRecognitionToggles = show;
  save();
}

QString TouchPresetManager::activePreset() const { return m_active; }

void TouchPresetManager::setActivePreset(const QString &name) {
  if (!m_presets.contains(name)) return;
  m_active = name;
  save();
}

QStringList TouchPresetManager::presetNames() const {
  return m_presets.keys();
}

bool TouchPresetManager::hasPreset(const QString &name) const {
  return m_presets.contains(name);
}

void TouchPresetManager::createPreset(const QString &name) {
  if (name.isEmpty() || m_presets.contains(name)) return;
  m_presets[name] = factoryMap();
  save();
}

void TouchPresetManager::duplicatePreset(const QString &from, const QString &to) {
  if (to.isEmpty() || !m_presets.contains(from) || m_presets.contains(to))
    return;
  m_presets[to] = m_presets.value(from);
  save();
}

bool TouchPresetManager::renamePreset(const QString &from, const QString &to) {
  if (to.isEmpty() || !m_presets.contains(from) || m_presets.contains(to))
    return false;
  m_presets[to] = m_presets.take(from);
  if (m_active == from) m_active = to;
  save();
  return true;
}

bool TouchPresetManager::removePreset(const QString &name) {
  if (m_presets.size() <= 1 || !m_presets.contains(name)) return false;
  m_presets.remove(name);
  if (m_active == name) m_active = m_presets.keys().first();
  save();
  return true;
}

void TouchPresetManager::resetPreset(const QString &name) {
  if (!m_presets.contains(name)) return;
  m_presets[name] = factoryMap();
  save();
}

QString TouchPresetManager::commandId(TouchSlot slot) const {
  return commandId(m_active, slot);
}

QString TouchPresetManager::commandId(const QString &preset,
                                      TouchSlot slot) const {
  return m_presets.value(preset).value(slot);
}

void TouchPresetManager::setCommandId(const QString &preset, TouchSlot slot,
                                      const QString &id) {
  if (!m_presets.contains(preset) || touchSlotIsReserved(slot)) return;
  if (id.isEmpty())
    m_presets[preset].remove(slot);
  else
    m_presets[preset][slot] = id;
  save();
}

bool TouchPresetManager::hasCommand(TouchSlot slot) const {
  return !commandId(slot).isEmpty();
}

void TouchPresetManager::execute(TouchSlot slot) {
  const QString id = commandId(slot);
  if (id.isEmpty()) return;
  QAction *action =
      CommandManager::instance()->getAction(id.toStdString().c_str());
  if (action) action->trigger();
}
