#include "toonz/xsheetengine.h"

#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshcolumn.h"
#include "tconvert.h"

#include <algorithm>

TXshLevel* XsheetEngine::singleLevel(TXshColumn* column) {
  TXshCellColumn* cells = column ? column->getCellColumn() : nullptr;
  if (!cells) return nullptr;

  TXshLevel* level = nullptr;
  int first, last;
  cells->getRange(first, last);
  for (int row = first; row <= last; ++row) {
    const TXshCell& cell = cells->getCell(row);
    if (cell.isEmpty()) continue;
    TXshLevel* current = cell.m_level.getPointer();
    if (level && current != level) return nullptr;
    level = current;
  }
  return level;
}

bool XsheetEngine::containsSingleLevel(TXshColumn* column,
                                       const std::string& columnName) {
  TXshLevel* level = singleLevel(column);
  return level &&
         (columnName.empty() || level->getName() == to_wstring(columnName));
}

XsheetEngine::ToggleResult XsheetEngine::toggleColumns(
    TXsheet* sheet, ColumnToggle toggle, const ColumnToggleOptions& options) {
  ToggleResult result;
  if (!sheet) return result;

  const int count = sheet->getColumnCount();
  for (int col = options.includeCamera ? -1 : 0; col < count; ++col) {
    TXshColumn* column = sheet->getColumn(col);
    if (!column || (options.targets == ColumnTargets::NonEmpty &&
                    sheet->isColumnEmpty(col)))
      continue;
    if (options.excludePaletteAndSoundText &&
        (column->getPaletteColumn() || column->getSoundTextColumn()))
      continue;

    switch (toggle) {
    case ColumnToggle::Preview:
      column->setPreviewVisible(!column->isPreviewVisible());
      break;
    case ColumnToggle::Camstand:
      column->setCamstandVisible(!column->isCamstandVisible());
      if (options.unifyVisibility)
        column->setPreviewVisible(column->isCamstandVisible());
      result.soundChanged |= column->getSoundColumn() != nullptr;
      break;
    case ColumnToggle::Lock:
      column->lock(!column->isLocked());
      break;
    }
    result.changed = true;
  }
  return result;
}

CellRange XsheetEngine::shiftCellRange(const CellRange& range,
                                       const CellPosition& delta,
                                       int firstColumn) {
  const int row = std::max(0, range.from().frame() + delta.frame());
  const int col = std::max(firstColumn, range.from().layer() + delta.layer());
  const int frameDelta  = row - range.from().frame();
  const int columnDelta = col - range.from().layer();
  return CellRange(CellPosition(row, col),
                   CellPosition(range.to().frame() + frameDelta,
                                range.to().layer() + columnDelta));
}

bool XsheetEngine::resizeCellRange(const CellRange& range,
                                   const CellPosition& delta,
                                   const CellPosition& pivot, int firstColumn,
                                   CellRange& result) {
  int rowA = range.from().frame(), rowB = range.to().frame();
  int colA = range.from().layer(), colB = range.to().layer();
  if (rowA < pivot.frame())
    rowA += delta.frame();
  else
    rowB += delta.frame();
  if (colA < pivot.layer())
    colA += delta.layer();
  else
    colB += delta.layer();

  if (rowA < 0 || rowB < 0 || colA < firstColumn || colB < firstColumn)
    return false;
  result = CellRange(CellPosition(rowA, colA), CellPosition(rowB, colB));
  return true;
}
