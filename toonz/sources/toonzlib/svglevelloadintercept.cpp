#include "toonz/toonzscene.h"

#ifdef _WIN32

#include "../toonz/svglevelloader.h"

#include <QByteArray>
#include <QtGlobal>

namespace {

bool retainedSvgTestPathEnabled() {
  const QByteArray value = qgetenv("OPENTOONZ_RETAINED_SVG").trimmed().toLower();
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

TXshLevel *loadLevelWithRetainedSvgTestPath(
    ToonzScene *scene, const TFilePath &actualPath,
    const LevelOptions *levelOptions, std::wstring levelName,
    const std::vector<TFrameId> &fIds) {
  // Keep ordinary SVG behavior unchanged. The retained-source path is exposed
  // only for foundation testing until identity/persistence is fully hardened.
  if (!scene->isLoading() && actualPath.getType() == "svg" &&
      retainedSvgTestPathEnabled()) {
    return SvgLevel::loadRetainedLevel(scene, actualPath, levelName);
  }

  return scene->loadLevel(actualPath, levelOptions, levelName, fIds);
}

}  // namespace

TXshLevel *ToonzScene::loadLevel(const TFilePath &actualPath) {
  return loadLevelWithRetainedSvgTestPath(this, actualPath, nullptr, L"",
                                          std::vector<TFrameId>());
}

TXshLevel *ToonzScene::loadLevel(const TFilePath &actualPath,
                                 const LevelOptions *levelOptions) {
  return loadLevelWithRetainedSvgTestPath(this, actualPath, levelOptions, L"",
                                          std::vector<TFrameId>());
}

TXshLevel *ToonzScene::loadLevel(const TFilePath &actualPath,
                                 const LevelOptions *levelOptions,
                                 std::wstring levelName) {
  return loadLevelWithRetainedSvgTestPath(this, actualPath, levelOptions,
                                          levelName,
                                          std::vector<TFrameId>());
}

#endif
