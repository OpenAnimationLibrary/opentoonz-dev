// Run against the built OpenToonz libraries, including the Windows native
// streams. No palette, scene, project or filesystem implementations are mocked.
#include "toonz/fullcolorpalette.h"
#include "toonz/toonzscene.h"
#include "toonz/tproject.h"
#include "tenv.h"
#include "tpalette.h"
#include "tsimplecolorstyles.h"
#include "tstream.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

TPaletteP readPalette(const TFilePath &path) {
  TIStream input(path);
  check(bool(input), "Could not open saved palette");
  TPersist *object = nullptr;
  input >> object;
  TPaletteP palette(dynamic_cast<TPalette *>(object));
  check(bool(palette), "Could not read saved palette");
  return palette;
}
}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    QTemporaryDir root(QDir::tempPath() + "/Raster Palette (save) XXXXXX");
    check(root.isValid(), "Could not create isolated test root");
    // Match portable build paths with spaces/parentheses and include Unicode.
    const QString stuff =
        root.path() + QString::fromUtf8("/portablestuff-\xc3\xa9");
    const std::string prefix = TEnv::getSystemVarPrefix();
    check(TEnv::setArgPathValue(prefix + "STUDIOPALETTE",
                                (stuff + "/studiopalette").toStdString()),
          "Could not redirect the test Studio Palette folder");
    TEnv::setStuffDir(TFilePath(stuff));
    TColorStyle::declare(new TSolidColorStyle());
    FullColorPalette *fc = FullColorPalette::instance();
    fc->clear();
    TPaletteP palette(new TPalette());
    palette->getPage(0)->addStyle(TPixel32(12, 34, 56, 255));
    palette->setDirtyFlag(true);

    // Creating and overwriting the global default must both work on Windows.
    for (int value : {12, 78}) {
      palette->getStyle(2)->setMainColor(TPixel32(value, 34, 56, 255));
      fc->saveAsDefault(palette.getPointer());
      check(palette->getDirtyFlag(), "Default save cleared project edits");
      check(readPalette(fc->getDefaultPalettePath())
                    ->getStyle(2)
                    ->getMainColor() == TPixel32(value, 34, 56, 255),
            "Saved default did not restore the selected color");
    }
    check(QDir(fc->getDefaultPalettePath().getParentDir().getQString())
                  .entryList(QDir::AllEntries | QDir::NoDotAndDotDot) ==
              QStringList{"Raster_Drawing_Palette.tpl"},
          "Default save left a temporary snapshot behind");

    // Exercise the same writer through the real project-palette save API.
    auto project = std::make_shared<TProject>();
    const TFilePath palettes(root.path() + "/project/palettes");
    project->setFolder("palettes", palettes);
    ToonzScene scene;
    scene.setProject(project);
    scene.setScenePath(TFilePath(root.path() + "/project/scenes/test.tnz"),
                       true);
    TPalette *live = fc->getPalette(&scene);
    for (int value : {90, 123}) {
      live->getStyle(2)->setMainColor(TPixel32(value, 34, 56, 255));
      live->setDirtyFlag(true);
      fc->savePalette(&scene);
      check(!fc->getDirtyFlag(), "Project save retained the dirty flag");
      check(readPalette(scene.decodeFilePath(fc->getPath()))
                    ->getStyle(2)
                    ->getMainColor() == TPixel32(value, 34, 56, 255),
            "Saved project palette did not restore the selected color");
    }
    fc->clear();
    std::cout
        << "PASS native raster palette create/overwrite/reload and cleanup\n";
    return 0;
  } catch (const TException &error) {
    std::wcerr << error.getMessage() << std::endl;
  } catch (const std::exception &error) {
    std::cerr << error.what() << std::endl;
  }
  return 1;
}
