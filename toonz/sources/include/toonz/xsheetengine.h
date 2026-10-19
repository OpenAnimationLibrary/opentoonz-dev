#pragma once

#ifndef XSHEETENGINE_INCLUDED
#define XSHEETENGINE_INCLUDED

#include "tcommon.h"
#include "cellposition.h"

#include <string>

#undef DVAPI
#undef DVVAR
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

class TXsheet;
class TXshColumn;
class TXshLevel;

// Shared document operations in frame/column coordinates. Callers retain
// responsibility for undo, application notifications and view interaction.
class DVAPI XsheetEngine final {
public:
  XsheetEngine() = delete;

  // Non-owning result, valid until the column's cells are edited. Queries are
  // uncached so edits from any view are immediately reflected.
  static TXshLevel* singleLevel(TXshColumn* column);
  static bool containsSingleLevel(TXshColumn* column,
                                  const std::string& columnName);

  enum class ColumnToggle { Preview, Camstand, Lock };
  // NonEmpty follows TXsheet's existing emptiness rules, including the camera.
  enum class ColumnTargets { NonEmpty, Existing };

  struct ColumnToggleOptions {
    ColumnTargets targets           = ColumnTargets::NonEmpty;
    bool includeCamera              = false;
    bool excludePaletteAndSoundText = false;
    bool unifyVisibility            = false;
  };

  struct ToggleResult {
    bool changed      = false;
    bool soundChanged = false;
  };

  static ToggleResult toggleColumns(TXsheet* sheet, ColumnToggle toggle,
                                    const ColumnToggleOptions& options);

  // Ranges are inclusive. The view resolves keys/gestures into a logical delta
  // and supplies its first selectable column (-1 when the camera is included).
  static CellRange shiftCellRange(const CellRange& range,
                                  const CellPosition& delta, int firstColumn);
  // Returns false without changing result if either moved boundary is invalid.
  static bool resizeCellRange(const CellRange& range, const CellPosition& delta,
                              const CellPosition& pivot, int firstColumn,
                              CellRange& result);
};

#endif  // XSHEETENGINE_INCLUDED
