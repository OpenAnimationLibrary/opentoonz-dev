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
class TXshSvgLevel;

namespace SvgLevel {

// Foundation-only retained SVG creation path.
//
// The Xsheet level retains SVG identity. Raster images are disposable
// representations supplied to consumers through ImageManager; they are not
// the level's source type.
SVGLEVEL_API TXshLevel *loadRetainedLevel(
    ToonzScene *scene, const TFilePath &actualSvgPath,
    const std::wstring &requestedName = L"");

// Rebuild ImageManager representation bindings for a persisted retained SVG
// level. This path is intentionally non-interactive for scene reopen.
SVGLEVEL_API bool restoreRetainedLevel(TXshSvgLevel *level);

// Save Level As semantics for retained SVG: copy the authoritative SVG source
// (or every frame of an SVG sequence) to another SVG path. This never writes a
// generated raster representation to disk. Saving back onto the authoritative
// source throws a read-only TSystemException with guidance for the user.
SVGLEVEL_API void saveRetainedCopy(TXshSvgLevel *level,
                                   const TFilePath &destinationPath);

}  // namespace SvgLevel

#undef SVGLEVEL_API

#endif