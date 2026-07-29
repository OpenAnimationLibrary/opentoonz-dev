#pragma once

#ifdef _WIN32

#include "tfilepath.h"

#include <string>

class ToonzScene;
class TXshLevel;
class QWidget;

namespace SvgLevel {

enum class OpenMode {
  Cancel,
  OpenExperimentalSvgLevel,
  ConvertToToonzVector
};

OpenMode askOpenMode(QWidget *parent = nullptr);

// Creates a read-only SVG_XSHLEVEL whose first frame is a full-color raster
// generated from the retained SVG source. The SVG-specific image builder keeps
// subsequent cache rebuilds on the same retained-source rasterization path.
// The returned level is inserted into the scene cast. Returns nullptr when
// loading or rasterization fails.
TXshLevel *loadExperimentalLevel(ToonzScene *scene,
                                 const TFilePath &actualSvgPath,
                                 const std::wstring &requestedName = L"");

}  // namespace SvgLevel

#endif  // _WIN32
