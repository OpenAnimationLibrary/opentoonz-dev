#pragma once

#ifndef EXTERNALSOURCESYNC_INCLUDED
#define EXTERNALSOURCESYNC_INCLUDED

#include "tapp.h"

#include "toonz/txshsimplelevel.h"
#include "toonz/toonzscene.h"
#include "toonz/levelset.h"
#include "toonz/txsheethandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/txshlevelhandle.h"

#include "toonzqt/dvdialog.h"
#include "toonzqt/icongenerator.h"

#include <map>

namespace ExternalSourceSync {

enum class Action { Ask, LoadUpdated, KeepCached, Cancel };

inline Action &operationAction() {
  static Action action = Action::Ask;
  return action;
}

inline Action &sessionAction() {
  static Action action = Action::Ask;
  return action;
}

inline int &operationDepth() {
  static int depth = 0;
  return depth;
}

inline std::map<TFilePath, LevelSourceFingerprint> &ignoredSourceVersions() {
  static std::map<TFilePath, LevelSourceFingerprint> versions;
  return versions;
}

// "Load/Keep All" is scoped to one explicit resource-load operation. The
// optional remembered choice lasts only until OpenToonz closes. Neither state
// is serialized into the scene or Preferences.
class OperationScope {
public:
  OperationScope() {
    if (operationDepth()++ == 0) operationAction() = Action::Ask;
  }

  ~OperationScope() {
    if (--operationDepth() == 0) operationAction() = Action::Ask;
  }
};

// Resolve a changed single-file source. A plain "Keep Cached Version" records
// only the exact disk fingerprint being ignored. If that source changes again,
// the new fingerprint is different and OpenToonz asks again.
inline Action resolve(TXshSimpleLevel *sl, const TFilePath &decodedPath) {
  if (!sl || !sl->hasAcceptedSourceFingerprint() ||
      !sl->hasSourceChanged(decodedPath))
    return Action::KeepCached;

  LevelSourceFingerprint current = sl->getCurrentSourceFingerprint(decodedPath);

  // A missing/unreadable source is not safe to destructively reload. Missing
  // source recovery can get its own explicit UI without risking cached data.
  if (!current.m_valid) return Action::KeepCached;

  auto &ignoredVersions = ignoredSourceVersions();
  auto ignored          = ignoredVersions.find(current.m_path);
  if (ignored != ignoredVersions.end() && ignored->second == current)
    return Action::KeepCached;

  Action action = sessionAction() != Action::Ask ? sessionAction()
                                                 : operationAction();

  if (action != Action::Ask) {
    if (action == Action::KeepCached)
      ignoredVersions[current.m_path] = current;
    else if (action == Action::LoadUpdated)
      ignoredVersions.erase(current.m_path);
    return action;
  }

  QString label =
      QObject::tr("The source file for level '%1' has changed outside "
                  "OpenToonz.\n\nWhat would you like to do?")
          .arg(QString::fromStdWString(sl->getName()));

  if (sl->getDirtyFlag())
    label += QObject::tr(
        "\n\nThe cached version contains unsaved changes. Loading the "
        "updated file will replace them.");

  QStringList buttons;
  buttons << QObject::tr("Load Updated File")
          << QObject::tr("Keep Cached Version")
          << QObject::tr("Load All Updated Files")
          << QObject::tr("Keep All Cached Versions")
          << QObject::tr("Cancel");

  DVGui::MessageAndCheckboxDialog *dialog = DVGui::createMsgandCheckbox(
      DVGui::QUESTION, label,
      QObject::tr("Remember this choice for this session."), buttons, 0,
      Qt::Unchecked);

  int ret     = dialog->exec();
  int checked = dialog->getChecked();
  dialog->deleteLater();

  switch (ret) {
  case 1:
    action = Action::LoadUpdated;
    break;
  case 2:
    action = Action::KeepCached;
    break;
  case 3:
    action            = Action::LoadUpdated;
    operationAction() = action;
    break;
  case 4:
    action            = Action::KeepCached;
    operationAction() = action;
    break;
  default:
    return Action::Cancel;
  }

  if (checked) sessionAction() = action;

  if (action == Action::KeepCached)
    ignoredVersions[current.m_path] = current;
  else
    ignoredVersions.erase(current.m_path);

  return action;
}

inline bool reload(TXshSimpleLevel *sl, const TFilePath &decodedPath) {
  if (!sl) return false;

  std::vector<TFrameId> oldFids;
  sl->getFids(oldFids);

  try {
    sl->load();
  } catch (...) {
    DVGui::error(QObject::tr("The updated source could not be loaded."));
    return false;
  }

  std::vector<TFrameId> newFids;
  sl->getFids(newFids);
  oldFids.insert(oldFids.end(), newFids.begin(), newFids.end());

  for (const TFrameId &fid : oldFids)
    IconGenerator::instance()->invalidate(sl, fid);
  IconGenerator::instance()->invalidate(decodedPath);

  TApp *app = TApp::instance();
  app->getCurrentXsheet()->notifyXsheetChanged();
  app->getCurrentScene()->notifyCastChange();
  if (app->getCurrentLevel()->getSimpleLevel() == sl)
    app->getCurrentLevel()->notifyLevelChange();

  return true;
}


template <class ResourceContainer>
inline bool synchronize(const ResourceContainer &resources) {
  TApp *app = TApp::instance();
  if (!app || !app->getCurrentScene()) return true;

  ToonzScene *scene = app->getCurrentScene()->getScene();
  if (!scene) return true;

  OperationScope operationScope;

  for (const auto &resource : resources) {
    TFilePath actualPath = scene->decodeFilePath(resource.m_path);
    if (actualPath.isEmpty()) continue;

    TXshSimpleLevel *sl = nullptr;
    TLevelSet *levelSet = scene->getLevelSet();
    if (!levelSet) continue;

    for (int i = 0; i < levelSet->getLevelCount(); ++i) {
      TXshLevel *level = levelSet->getLevel(i);
      if (!level) continue;
      if (scene->decodeFilePath(level->getPath()) != actualPath) continue;
      sl = level->getSimpleLevel();
      break;
    }

    if (!sl || !sl->hasAcceptedSourceFingerprint() ||
        !sl->hasSourceChanged(actualPath))
      continue;

    Action action = resolve(sl, actualPath);
    if (action == Action::Cancel) return false;
    if (action == Action::LoadUpdated && !reload(sl, actualPath)) return false;
  }

  return true;
}

}  // namespace ExternalSourceSync

#endif  // EXTERNALSOURCESYNC_INCLUDED
