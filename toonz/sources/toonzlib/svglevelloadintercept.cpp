#include "toonz/toonzscene.h"

#ifdef _WIN32

#include "../toonz/svglevelloader.h"

namespace {

TXshLevel *loadLevelWithSvgChoice(ToonzScene *scene,
                                  const TFilePath &actualPath,
                                  const LevelOptions *levelOptions,
                                  std::wstring levelName,
                                  const std::vector<TFrameId> &fIds) {
  // Scene resource loading must remain non-interactive. This first milestone
  // only asks when a user explicitly introduces an SVG through the normal
  // level-loading commands.
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

  // The established four-argument loader remains unchanged. In particular,
  // choosing conversion continues through the existing SVG-to-PLI path.
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

#endif  // _WIN32
