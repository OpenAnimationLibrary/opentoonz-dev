// Production geometry and scene-metadata regression coverage. A geometry-only
// build can run without the rest of toonzlib; the normal CMake target also
// exercises column cloning, replacement, movement, and actual scene streams.
#include "toonz/columnfan.h"
#include "toonz/preferences.h"
#include "orientation.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsoundtextlevel.h"
#include "toonz/txshcolumn.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/txshsoundcolumn.h"
#include "toonz/txshsoundtextcolumn.h"
#include "toonz/txshpalettecolumn.h"
#include "toonz/txshmeshcolumn.h"
#include "toonz/txshzeraryfxcolumn.h"
#include "toonz/toonzfolders.h"
#include "tstream.h"
#include "tenv.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void checkGeometry() {
  Preferences *prefs                                  = Preferences::instance();
  prefs->m_items[xsheetLayoutPreference].value        = QString("Adjustable");
  prefs->m_items[xsheetColumnWidth].value             = 74;
  prefs->m_items[showKeyframesOnXsheetCellArea].value = true;
  for (bool camera : {false, true}) {
    prefs->m_items[showXsheetCameraColumn].value = camera;
    for (int mask = 0; mask < 64; ++mask) {
      ColumnFan fan;
      const std::vector<int> widths{50, 120, 0, 2048, 85, 200};
      fan.setWidths(widths);
      for (int col = 0; col < 6; ++col)
        if (mask & (1 << col)) fan.deactivate(col);
      int expected = camera ? 22 : 0;
      for (int col = 0; col < 10; ++col) {
        require(fan.colToLayerAxis(col) == expected, "Column origin mismatch");
        if (fan.isActive(col)) {
          int width = col < 6 && widths[col] ? widths[col] : 74;
          require(fan.unfoldedSize(col) == width, "Column width mismatch");
          for (int x = expected; x < expected + width; ++x)
            require(fan.layerAxisToCol(x) == col, "Active hit region mismatch");
          expected += width;
        } else if (col == 5 || fan.isActive(col + 1)) {
          for (int x = expected; x < expected + 9; ++x)
            require(fan.layerAxisToCol(x) == col, "Folded hit region mismatch");
          expected += 9;
        }
      }
      // Unfolding the last fan entries must retain width-only columns.
      for (int col = 0; col < 6; ++col) fan.activate(col);
      require(fan.colToLayerAxis(4) == (camera ? 22 : 0) + 50 + 120 + 74 + 2048,
              "Unfolding discarded a width override");
      fan.setWidths({});
      require(fan.colToLayerAxis(5) == (camera ? 22 : 0) + 5 * 74,
              "Reset did not restore default positions");
    }
  }
  const Orientation *base = Orientations::topToBottom();
  for (int width = 50; width <= 2048; ++width) {
    const Orientation *o = Orientations::withColumnWidth(width);
    require(o->cellWidth() == width, "Width geometry mismatch");
    require(o->rect(PredefinedRect::CELL).width() == width,
            "Cell rectangle mismatch");
    require(o->rect(PredefinedRect::KEY_ICON).right() < width,
            "Key outside column");
    require(o->rect(PredefinedRect::LOCK_AREA).right() < width,
            "Lock outside column");
    require(o->rect(PredefinedRect::CONFIG_AREA).right() < width,
            "Config outside column");
    require(o->rect(PredefinedRect::VOLUME_AREA).right() < width,
            "Volume outside column");
    require(o->dimension(PredefinedDimension::CAMERA_LAYER) == 22,
            "Camera changed");
    require(base->cellWidth() == 74, "Shared geometry was mutated");
    require(o == Orientations::withColumnWidth(width),
            "Geometry pointer is unstable");
  }
  require(Orientations::leftToRight()->cellHeight() == 24,
          "Timeline geometry changed");
}
#ifndef COLUMN_GEOMETRY_ONLY
void checkColumns(const QString &path) {
  TXsheetP sheet(new TXsheet());
  for (int col = 0; col < 3; ++col) sheet->insertColumn(col);
  sheet->setColumnWidthOverride(0, 120);
  sheet->setColumnWidthOverride(2, 300);
  sheet->setColumnWidthOverride(6, 500);  // empty trailing column
  sheet->getColumnFan(Orientations::topToBottom())->deactivate(1);
  sheet->moveColumn(0, 2);
  require(sheet->getColumnWidthOverride(2) == 120,
          "Moved width did not follow column");
  require(sheet->getColumnWidthOverride(1) == 300,
          "Neighbor width lost on move");
  sheet->insertColumn(1);
  require(sheet->getColumnWidthOverride(1) == 0,
          "Inserted column did not inherit default");
  require(sheet->getColumnWidthOverride(2) == 300, "Insert lost width");
  sheet->removeColumn(1);
  require(sheet->getColumnWidthOverride(1) == 300, "Remove lost width");
  // Replacing an empty level column with another type must preserve its width.
  TXshSoundTextLevelP notes(new TXshSoundTextLevel(L"notes"));
  notes->setType(SND_TXT_XSHLEVEL);
  notes->setFrameText(0, "Column width regression");
  require(sheet->setCell(0, 1, TXshCell(notes.getPointer(), TFrameId(1))),
          "Could not replace empty column with Note text");
  require(sheet->getColumnWidthOverride(1) == 300,
          "Type replacement lost width");
  for (int type = TXshColumn::eLevelType; type <= TXshColumn::eMeshType;
       ++type) {
    TXshColumnP column(TXshColumn::createEmpty(type));
    column->setXsheetColumnWidth(123);
    TXshColumnP copy(column->clone());
    require(copy->getXsheetColumnWidth() == 123, "Clone lost width");
  }
  {
    TOStream stream{TFilePath(path)};
    sheet->saveData(stream);
  }
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "Cannot inspect saved scene");
  QByteArray contents = file.readAll();
  require(contents.contains("widths=\""), "Width attribute was not saved");
  require(!contents.contains("<columnWidths>"),
          "Incompatible scene tag introduced");
  TXsheetP loaded(new TXsheet());
  {
    TIStream stream{TFilePath(path)};
    loaded->loadData(stream);
  }
  for (int col = 0; col < sheet->getColumnCount(); ++col)
    require(sheet->getColumnWidthOverride(col) ==
                loaded->getColumnWidthOverride(col),
            "Scene reload lost width");
  require(loaded->getColumnWidthOverride(6) == 500,
          "Empty trailing width was lost");
  loaded->setColumnWidthOverride(6, 0);
  require(
      loaded->getColumnFan(Orientations::topToBottom())->unfoldedSize(6) == 74,
      "Reset did not restore the default");
  require(
      loaded->getColumnFan(Orientations::leftToRight())->unfoldedSize(6) == 24,
      "Xsheet width leaked into Timeline");
}
#endif
}  // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  try {
#ifndef COLUMN_GEOMETRY_ONLY
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create test settings directory");
    QDir root(directory.path());
    require(root.mkpath("profiles/layouts/rooms/Default"),
            "Cannot create test rooms");
    TEnv::setSystemVarPrefix("XSHEET_COLUMN_WIDTH_TEST_");
    TEnv::setStuffDir(TFilePath(directory.path()));
    require(QDir().mkpath(ToonzFolder::getMyModuleDir().getQString()),
            "Cannot isolate preferences");
#endif
    checkGeometry();
#ifndef COLUMN_GEOMETRY_ONLY
    checkColumns(directory.filePath("widths.tnzs"));
#endif
    std::cout << "Xsheet column width regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
