// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#ifndef EXR_LEVEL_LOADER_H
#define EXR_LEVEL_LOADER_H

#include "tfilepath.h"

#include <string>
#include <vector>

#undef DVAPI
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif

class ToonzScene;
class TXshExrLevel;
class LevelOptions;

namespace ExrLevel {

// A displayable image carried by an EXR document.  The part identifier is
// stable when the source names its parts; partIndexFallback keeps unnamed
// multipart files addressable when they are reopened.
struct Binding {
  std::string part;
  int partIndexFallback = -1;
  std::string layer;
  std::wstring label;
  // Data-like bindings (normals, depth, IDs, vectors) bypass display gamma.
  bool raw     = false;
  bool primary = false;
};

//! Enumerate the flat, displayable part/layer bindings in the first frame.
DVAPI std::vector<Binding> inspectBindings(const TFilePath &path,
                                           std::string *error = nullptr);

//! Find a loaded EXR level by source path and binding, not path alone.
DVAPI TXshExrLevel *findRetainedLevel(ToonzScene *scene, const TFilePath &path,
                                      const Binding &binding);

//! Load and insert a source-backed EXR level. Existing matching levels win.
DVAPI TXshExrLevel *loadRetainedLevel(
    ToonzScene *scene, const TFilePath &path, const Binding &binding,
    const std::wstring &levelName     = std::wstring(),
    const std::vector<TFrameId> &fids = std::vector<TFrameId>(),
    const LevelOptions *levelOptions  = nullptr);

//! Rebuild lazy image bindings from the level's source and selector.
DVAPI void restoreRetainedLevel(TXshExrLevel *level);
DVAPI void restoreRetainedLevel(TXshExrLevel *level,
                                const std::vector<TFrameId> &fids);

//! Save Level As support: byte-copy the source document(s), never re-encode.
DVAPI bool saveRetainedCopy(TXshExrLevel *level, const TFilePath &path,
                            const TFilePath &oldPath = TFilePath(),
                            bool overwrite           = true);

}  // namespace ExrLevel

#endif  // EXR_LEVEL_LOADER_H
