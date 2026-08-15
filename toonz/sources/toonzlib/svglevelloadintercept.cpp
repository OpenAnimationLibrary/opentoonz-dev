#include "toonz/toonzscene.h"

#ifdef _WIN32

#include "../toonz/svglevelloader.h"

namespace {

TXshLevel *loadLevelWithSvgChoice(ToonzScene *scene,
                                  const TFilePath &actualPath,
                                  const LevelOptions *levelOptions,
                                  std::wstring levelName,
                                  const std::vector<TFrameId> &fIds) {
  if (!scene->isLoading() && actualPath.getType() == "svg") {
    switch (SvgLevel::askOpenMode()) {
    case SvgLevel::OpenMode::Cancel:
      return nullptr;
    case SvgLevel::OpenMode::OpenExperimentalSvgLevel:
      return SvgLevel::loadExperimentalLevel(scene, actualPath, levelName);
    case SvgLevel::OpenMode::ConvertToToonzVector:
      break;
    }
  }

  return scene->loadLevel(actualPath, levelOptions, levelName, fIds);
}

}  // namespace

TXshLevel *ToonzScene::loadLevel(const TFilePath &actualPath) {
  return loadLevelWithSvgChoice(this, actualPath, nullptr, L"",
                                std::vector<TFrameId>());
}

TXshLevel *ToonzScene::loadLevel(const TFilePath &actualPath,
                                 const LevelOptions *levelOptions) {
  return loadLevelWithSvgChoice(this, actualPath, levelOptions, L"",
                                std::vector<TFrameId>());
}

TXshLevel *ToonzScene::loadLevel(const TFilePath &actualPath,
                                 const LevelOptions *levelOptions,
                                 std::wstring levelName) {
  return loadLevelWithSvgChoice(this, actualPath, levelOptions, levelName,
                                std::vector<TFrameId>());
}

#endif
