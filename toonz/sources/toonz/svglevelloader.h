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
// This deliberately exposes no reload, relink, validation, editor, or UI
// services. The SVG remains authoritative and OpenToonz binds a disposable
// read-only raster representation for display.
SVGLEVEL_API TXshLevel *loadRetainedLevel(
    ToonzScene *scene, const TFilePath &actualSvgPath,
    const std::wstring &requestedName = L"");

}  // namespace SvgLevel

#undef SVGLEVEL_API

#endif
