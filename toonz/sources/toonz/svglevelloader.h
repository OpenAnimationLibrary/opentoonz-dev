#pragma once

#ifdef _WIN32

#include "tcommon.h"
#include "tfilepath.h"

#include <QByteArray>
#include <QSize>
#include <QString>

#include <string>

#undef SVGLEVEL_API
#ifdef TOONZLIB_EXPORTS
#define SVGLEVEL_API DV_EXPORT_API
#else
#define SVGLEVEL_API DV_IMPORT_API
#endif

class ToonzScene;
class TXshLevel;
class TXshSimpleLevel;
class QWidget;

namespace SvgLevel {

enum class OpenMode {
  Cancel,
  OpenExperimentalSvgLevel,
  ConvertToToonzVector
};

SVGLEVEL_API OpenMode askOpenMode(QWidget *parent = nullptr);

// Validates well-formed XML and verifies that the current Windows SVG renderer
// can parse the document. Optional line and column values are supplied for XML
// errors, while naturalSize reports the renderer's intrinsic dimensions.
SVGLEVEL_API bool validateSource(const QByteArray &source, QString *error = nullptr,
                                 int *line = nullptr, int *column = nullptr,
                                 QSize *naturalSize = nullptr);

// Returns the concrete physical source path for an SVG Level, resolving project
// aliases and OpenToonz's empty-frame level notation.
SVGLEVEL_API TFilePath sourcePathForLevel(const TXshSimpleLevel *level);

// Recreates frame 1 and its SVG-specific image builder for a level restored from
// scene data. The level remains present even when its source is unavailable.
SVGLEVEL_API bool restoreExperimentalLevel(TXshSimpleLevel *level,
                                           QString *error = nullptr);

// Rasterizes the current source transactionally and replaces the generated
// display frame only after the new SVG has been parsed successfully.
SVGLEVEL_API bool reloadExperimentalLevel(TXshSimpleLevel *level,
                                          QString *error = nullptr);

// Changes the retained source path only after the replacement SVG has been
// parsed and rasterized successfully.
SVGLEVEL_API bool relinkExperimentalLevel(TXshSimpleLevel *level,
                                          const TFilePath &newSourcePath,
                                          QString *error = nullptr);

// Creates a read-only SVG_XSHLEVEL whose first frame is a full-color raster
// generated from the retained SVG source. The SVG-specific image builder keeps
// subsequent cache rebuilds on the same retained-source rasterization path.
// The returned level is inserted into the scene cast. Returns nullptr when
// loading or rasterization fails.
SVGLEVEL_API TXshLevel *loadExperimentalLevel(
    ToonzScene *scene, const TFilePath &actualSvgPath,
    const std::wstring &requestedName = L"");

}  // namespace SvgLevel

#undef SVGLEVEL_API

#endif  // _WIN32
