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
SVGLEVEL_API bool validateSource(const QByteArray &source,
                                 QString *error = nullptr,
                                 int *line = nullptr,
                                 int *column = nullptr,
                                 QSize *naturalSize = nullptr);
SVGLEVEL_API TFilePath sourcePathForLevel(const TXshSimpleLevel *level);
SVGLEVEL_API bool restoreExperimentalLevel(TXshSimpleLevel *level,
                                           QString *error = nullptr);
SVGLEVEL_API bool reloadExperimentalLevel(TXshSimpleLevel *level,
                                          QString *error = nullptr);
SVGLEVEL_API bool relinkExperimentalLevel(TXshSimpleLevel *level,
                                          const TFilePath &newSourcePath,
                                          QString *error = nullptr);
SVGLEVEL_API TXshLevel *loadExperimentalLevel(
    ToonzScene *scene, const TFilePath &actualSvgPath,
    const std::wstring &requestedName = L"");

}  // namespace SvgLevel

#undef SVGLEVEL_API

#endif
