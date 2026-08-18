#pragma once

#ifdef _WIN32

#include "tcommon.h"
#include "tfilepath.h"

#include <string>

#undef SVGLEVEL_API
#ifdef TOONZLIB_EXPORTS
#define SVGLEVEL_API DV_EXPORT_API
#else
#define SVGLEVEL_API DV_IMPORT_API
#endif

class ToonzScene;
class TXshLevel;

namespace SvgLevel {

// Foundation-only retained SVG creation path.
//
// The Xsheet level retains SVG identity. Raster images are disposable
// representations supplied to consumers through ImageManager; they are not
// the level's source type.
SVGLEVEL_API TXshLevel *loadRetainedLevel(
    ToonzScene *scene, const TFilePath &actualSvgPath,
    const std::wstring &requestedName = L"");

}  // namespace SvgLevel

#undef SVGLEVEL_API

#endif