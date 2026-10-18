#include "toonz/xsheetengine.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshpalettelevel.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txshsoundlevel.h"
#include "toonz/txshsoundtextlevel.h"
#include "tconvert.h"
#include "texception.h"
#include "tnzsound.h"
#include "tsound_io.h"

#include <QApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;

void require(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

void requireRange(const CellRange& range, int r0, int c0, int r1, int c1,
                  const char* message) {
  require(range.from().frame() == r0 && range.from().layer() == c0 &&
              range.to().frame() == r1 && range.to().layer() == c1,
          message);
}

void levelQueries() {
  TXsheetP sheet     = new TXsheet;
  TXshSimpleLevelP a = new TXshSimpleLevel(L"\u7dda_\u00e9");
  TXshSimpleLevelP b = new TXshSimpleLevel(a->getName());
  a->setType(PLI_XSHLEVEL);
  b->setType(PLI_XSHLEVEL);
  sheet->insertColumn(0);
  auto* column = sheet->getColumn(0);
  require(!XsheetEngine::singleLevel(nullptr), "null column has no level");
  require(!XsheetEngine::singleLevel(column), "empty column has no level");
  sheet->setCell(20, 0, TXshCell(a.getPointer(), TFrameId(1)));
  sheet->setCell(10000, 0, TXshCell(a.getPointer(), TFrameId(2)));
  require(XsheetEngine::singleLevel(column) == a.getPointer(),
          "gaps and different drawing numbers retain one level");
  require(XsheetEngine::containsSingleLevel(column, ""),
          "unspecified column name accepts a single level");
  require(XsheetEngine::containsSingleLevel(column, to_string(a->getName())),
          "Unicode level name matches");
  require(!XsheetEngine::containsSingleLevel(column, "custom column"),
          "a distinct column name remains visible");
  sheet->setCell(21, 0, TXshCell(b.getPointer(), TFrameId(1)));
  require(!XsheetEngine::singleLevel(column),
          "distinct levels with the same name remain distinct");
  sheet->clearCells(21, 0, 1);
  require(XsheetEngine::singleLevel(column) == a.getPointer(),
          "cell removal is reflected immediately");
  a->setName(L"renamed");
  require(XsheetEngine::containsSingleLevel(column, "renamed") &&
              !XsheetEngine::containsSingleLevel(column, "old name"),
          "renaming a level is reflected immediately");
}

struct Columns {
  QTemporaryDir directory;
  TXsheetP sheet            = new TXsheet;
  TXshSimpleLevelP drawing  = new TXshSimpleLevel(L"drawing");
  TXshPaletteLevelP palette = new TXshPaletteLevel(L"palette");
  TXshSoundTextLevelP text  = new TXshSoundTextLevel(L"text");
  TXshSoundLevelP sound     = new TXshSoundLevel;

  Columns() {
    drawing->setType(PLI_XSHLEVEL);
    text->setType(SND_TXT_XSHLEVEL);
    sound->setType(SND_XSHLEVEL);
    text->setFrameText(0, "text");
    require(directory.isValid(), "temporary sound fixture directory");
    const TFilePath path((directory.path() + "/sound.wav").toStdWString());
    require(TSoundTrackWriter::save(
                path, TSoundTrack::create(48000, 16, 1, 4800, TSound::INT)),
            "write sound fixture");
    sound->setFrameRate(24);
    sound->loadSoundTrack(path);
    require(sound->getFrameCount() >= 2, "sound fixture spans multiple frames");
    require(sheet->setCell(0, 0, TXshCell(drawing.getPointer(), TFrameId(1))),
            "create drawing exposure");
    sheet->insertColumn(1, new TXshLevelColumn);
    require(sheet->setCell(0, 2, TXshCell(palette.getPointer(), TFrameId(1))),
            "create palette exposure");
    require(sheet->setCell(0, 3, TXshCell(text.getPointer(), TFrameId(1))),
            "create sound-text exposure");
    require(sheet->setCell(0, 4, TXshCell(sound.getPointer(), TFrameId(0))) &&
                sheet->setCell(1, 4, TXshCell(sound.getPointer(), TFrameId(1))),
            "create sound exposures");
    require(sheet->getColumn(2)->getPaletteColumn() && !sheet->isColumnEmpty(2),
            "palette fixture is a populated palette column");
    require(
        sheet->getColumn(3)->getSoundTextColumn() && !sheet->isColumnEmpty(3),
        "text fixture is a populated sound-text column");
    require(!sheet->isColumnEmpty(4), "sound fixture is a populated column");
    for (int col = -1; col < sheet->getColumnCount(); ++col) {
      auto* column = sheet->getColumn(col);
      column->setPreviewVisible(true);
      column->setCamstandVisible(true);
      column->lock(false);
    }
  }
};

void columnToggles() {
  using Toggle  = XsheetEngine::ColumnToggle;
  using Targets = XsheetEngine::ColumnTargets;
  XsheetEngine::ColumnToggleOptions options;
  require(!XsheetEngine::toggleColumns(nullptr, Toggle::Lock, options).changed,
          "null sheet is unchanged");
  TXsheetP empty = new TXsheet;
  require(
      !XsheetEngine::toggleColumns(empty.getPointer(), Toggle::Lock, options)
           .changed,
      "empty target range is unchanged");

  Columns header;
  options.excludePaletteAndSoundText = true;
  const auto preview = XsheetEngine::toggleColumns(header.sheet.getPointer(),
                                                   Toggle::Preview, options);
  require(preview.changed && !preview.soundChanged,
          "preview change does not request a sound refresh");
  require(!header.sheet->getColumn(0)->isPreviewVisible() &&
              !header.sheet->getColumn(4)->isPreviewVisible(),
          "header preview affects drawing and sound columns");
  for (int col : {-1, 1, 2, 3})
    require(header.sheet->getColumn(col)->isPreviewVisible(),
            "header preview preserves camera, empty, palette and text columns");
  header.sheet->getColumn(0)->setPreviewVisible(true);
  const auto camstand = XsheetEngine::toggleColumns(header.sheet.getPointer(),
                                                    Toggle::Camstand, options);
  require(camstand.soundChanged, "sound column requests a sound refresh");
  require(!header.sheet->getColumn(0)->isCamstandVisible() &&
              header.sheet->getColumn(0)->isPreviewVisible(),
          "separate camstand toggle leaves preview alone");
  header.sheet->getColumn(0)->setPreviewVisible(false);
  options.unifyVisibility = true;
  XsheetEngine::toggleColumns(header.sheet.getPointer(), Toggle::Camstand,
                              options);
  require(header.sheet->getColumn(0)->isCamstandVisible() &&
              header.sheet->getColumn(0)->isPreviewVisible(),
          "unified camstand toggle synchronizes preview");
  options.includeCamera              = true;
  options.excludePaletteAndSoundText = false;
  XsheetEngine::toggleColumns(header.sheet.getPointer(), Toggle::Lock, options);
  require(!header.sheet->getColumn(-1)->isLocked() &&
              !header.sheet->getColumn(1)->isLocked(),
          "nonempty-only lock preserves empty camera and drawing columns");
  for (int col : {0, 2, 3, 4})
    require(header.sheet->getColumn(col)->isLocked(),
            "header lock includes populated special columns");

  Columns panel;
  options               = XsheetEngine::ColumnToggleOptions();
  options.includeCamera = true;
  options.targets       = Targets::Existing;
  XsheetEngine::toggleColumns(panel.sheet.getPointer(), Toggle::Camstand,
                              options);
  for (int col = -1; col < panel.sheet->getColumnCount(); ++col)
    require(!panel.sheet->getColumn(col)->isCamstandVisible() &&
                panel.sheet->getColumn(col)->isPreviewVisible(),
            "layer-panel policy includes empty and special columns without "
            "unifying");
  XsheetEngine::toggleColumns(panel.sheet.getPointer(), Toggle::Lock, options);
  require(panel.sheet->getColumn(-1)->isLocked() &&
              panel.sheet->getColumn(1)->isLocked(),
          "existing-column lock includes camera and empty columns");
}

void selectionRanges() {
  const CellRange range(CellPosition(5, 3), CellPosition(7, 5));
  requireRange(XsheetEngine::shiftCellRange(range, CellPosition(3, 0), 0), 8, 3,
               10, 5, "selection advances by its frame stride");
  requireRange(XsheetEngine::shiftCellRange(range, CellPosition(0, 1), 0), 5, 4,
               7, 6, "selection shifts by one logical column");
  requireRange(XsheetEngine::shiftCellRange(range, CellPosition(-10, -10), 0),
               0, 0, 2, 2, "boundary clamp preserves selection size");
  requireRange(XsheetEngine::shiftCellRange(range, CellPosition(-10, -10), -1),
               0, -1, 2, 1, "first-column policy is explicit");

  CellRange result;
  require(XsheetEngine::resizeCellRange(range, CellPosition(1, 1),
                                        CellPosition(5, 3), 0, result),
          "selection expands away from its first-cell pivot");
  requireRange(result, 5, 3, 8, 6, "last boundaries expand");
  require(XsheetEngine::resizeCellRange(range, CellPosition(-1, -1),
                                        CellPosition(7, 5), 0, result),
          "selection expands away from its last-cell pivot");
  requireRange(result, 4, 2, 7, 5, "first boundaries expand");
  const CellRange one(CellPosition(2, 1), CellPosition(2, 1));
  require(XsheetEngine::resizeCellRange(one, CellPosition(-1, 0),
                                        CellPosition(2, 1), 0, result),
          "crossing the pivot keeps a normalized range");
  requireRange(result, 1, 1, 2, 1, "crossed frame boundaries normalize");

  const CellRange origin(CellPosition(0, 0), CellPosition(2, 2));
  result = range;
  require(!XsheetEngine::resizeCellRange(origin, CellPosition(-1, 0),
                                         CellPosition(2, 2), 0, result),
          "negative frame resize is rejected");
  requireRange(result, 5, 3, 7, 5, "rejected resize preserves its output");
  require(!XsheetEngine::resizeCellRange(origin, CellPosition(0, -1),
                                         CellPosition(2, 2), 0, result),
          "hidden-camera boundary is respected");
  require(XsheetEngine::resizeCellRange(origin, CellPosition(0, -1),
                                        CellPosition(2, 2), -1, result),
          "camera boundary can be supplied explicitly");
  requireRange(result, 0, -1, 2, 2, "camera-inclusive logical resize");
}
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  try {
    initSoundIo();
    levelQueries();
    columnToggles();
    selectionRanges();
    std::cout << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
  } catch (const TException& error) {
    std::cerr << to_string(error.getMessage()) << '\n';
  }
  return 1;
}
