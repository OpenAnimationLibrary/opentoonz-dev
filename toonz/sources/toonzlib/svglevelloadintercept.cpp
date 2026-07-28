#include "toonz/toonzscene.h"

#ifdef _WIN32

#include "../toonz/svglevelloader.h"

TXshLevel *ToonzScene::loadLevel(const TFilePath &actualPath,
                                 const LevelOptions *levelOptions,
                                 std::wstring levelName,
                                 const std::vector<TFrameId> &fIds) {
  // Scene resource loading must remain non-interactive. This first milestone
  // only asks when a user explicitly introduces an SVG through the normal
  // level-loading commands.
  if (!isLoading() && actualPath.getType() == "svg") {
    switch (SvgLevel::askOpenMode()) {
    case SvgLevel::OpenMode::Cancel:
      return nullptr;

    case SvgLevel::OpenMode::OpenExperimentalSvgLevel:
      return SvgLevel::loadExperimentalLevel(this, actualPath, levelName);

    case SvgLevel::OpenMode::ConvertToToonzVector:
      break;
    }
  }

  // Preserve all established level loading and SVG-to-PLI conversion behavior
  // unless the user explicitly selected the experimental retained SVG route.
  return loadLevelImpl(actualPath, levelOptions, levelName, fIds);
}

#endif  // _WIN32
