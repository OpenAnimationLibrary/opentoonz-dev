#include "toonz/fullcolorpalette.h"
#include "toonz/toonzscene.h"
#include "tsimplecolorstyles.h"
#include "tpalette.h"
#include "tstream.h"
#include "tsystem.h"
#include "tvectorimage.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

TFilePath studioRoot;
static int assertions = 0;
namespace ToonzFolder {
TFilePath getStudioPaletteFolder() { return studioRoot; }
}  // namespace ToonzFolder

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++assertions;                                                              \
    if (!(condition))                                                          \
      throw std::runtime_error(std::string(#condition) + " at line " +         \
                               std::to_string(__LINE__));                      \
  } while (false)

struct Fixture {
  QTemporaryDir temporary;
  ToonzScene scene;
  FullColorPalette *fc;
  Fixture()
      : scene(TFilePath(temporary.path() + "/project/palettes"))
      , fc(FullColorPalette::instance()) {
    CHECK(temporary.isValid());
    fc->clear();
    studioRoot = TFilePath(temporary.path() + "/studio");
  }
  ~Fixture() { fc->clear(); }
  TFilePath current() { return scene.palettes + "Raster_Drawing_Palette.tpl"; }
  TFilePath legacy() { return scene.palettes + "fullcolorPalette.tpl"; }
  TFilePath global() { return fc->getDefaultPalettePath(); }
};

static QByteArray readBytes(const TFilePath &path) {
  QFile file(path.getQString());
  CHECK(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

static void writeBytes(const TFilePath &path, const QByteArray &bytes) {
  CHECK(TSystem::touchParentDir(path));
  QFile file(path.getQString());
  CHECK(file.open(QIODevice::WriteOnly));
  CHECK(file.write(bytes) == bytes.size());
}

static void writeRaw(const TFilePath &path, TPalette *palette) {
  CHECK(TSystem::touchParentDir(path));
  TOStream stream(path);
  CHECK(stream);
  stream << palette;
  CHECK(stream.checkStatus());
}

static TPaletteP readRaw(const TFilePath &path) {
  TIStream stream(path);
  CHECK(stream);
  TPersist *object = nullptr;
  stream >> object;
  TPalette *palette = dynamic_cast<TPalette *>(object);
  CHECK(palette);
  return TPaletteP(palette);
}

static QStringList directoryEntries(const TFilePath &path) {
  return QDir(path.getParentDir().getQString())
      .entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
}

static void expectThrow(const std::function<void()> &action) {
  bool thrown = false;
  try {
    action();
  } catch (const TException &) {
    thrown = true;
  }
  CHECK(thrown);
}

static void checkBasic(TPalette *palette) {
  CHECK(palette);
  CHECK(palette->getStyleCount() == 2);
  CHECK(palette->getStylePage(0));
  CHECK(palette->getStylePage(1));
  CHECK(palette->getStyle(0)->getMainColor() == TPixel32(255, 255, 255, 0));
  CHECK(palette->getStyle(1)->getMainColor() == TPixel32(0, 0, 0, 255));
  CHECK(palette->getPaletteName() == L"Raster Drawing Palette");
}

static TPaletteP makePalette(int seed = 0) {
  TPaletteP palette(new TPalette());
  palette->addStyle(TPixel32(20 + seed, 80, 130, 190));
  palette->addStyle(TPixel32(210, 30 + seed, 45, 240));
  palette->addStyle(TPixel32(70, 160, 40 + seed, 70));
  palette->addStyle(TPixel32(100, 50, 25, 0));
  palette->getPage(0)->setName(L"Primary colors");
  palette->getPage(0)->addStyle(3);
  auto *page = palette->addPage(L"Second page é");
  page->addStyle(4);
  page->addStyle(2);
  palette->getStyle(2)->setName(L"Ink é");
  palette->getStyle(2)->setGlobalName(L"-studio-palette-id-2");
  palette->getStyle(2)->setOriginalName(L"Original ink");
  palette->getStyle(2)->setIsEditedFlag(true);
  palette->getStyle(2)->setFlags(3);
  palette->getStyle(2)->setPickedPosition(TPoint(15, 28), 7);
  palette->getStyle(3)->setName(L"3rd color");
  palette->getStyle(4)->setName(L"_accent");
  palette->getStyle(5)->setName(L"Unpaged transparent");
  palette->setShortcutValue('8', 4);
  palette->setKeyframe(2, 0);
  palette->getStyle(2)->setMainColor(TPixel32(200, 80, 130, 190));
  palette->setKeyframe(2, 12);
  palette->setFrame(4);
  palette->setPaletteName(L"Live source palette");
  palette->setGlobalName(L"Source palette global identity");
  palette->setCurrentStyleId(4);
  palette->setIsLocked(true);
  palette->setDirtyFlag(true);
  return palette;
}

static void checkContents(const TPalette *actual, const TPalette *expected) {
  CHECK(actual->getStyleCount() == expected->getStyleCount());
  CHECK(actual->getPageCount() == expected->getPageCount());
  for (int id = 0; id < expected->getStyleCount(); ++id) {
    auto *a = actual->getStyle(id);
    auto *e = expected->getStyle(id);
    CHECK(a->getMainColor() == e->getMainColor());
    CHECK(a->getName() == e->getName());
    CHECK(a->getGlobalName() == e->getGlobalName());
    CHECK(a->getOriginalName() == e->getOriginalName());
    CHECK(a->getIsEditedFlag() == e->getIsEditedFlag());
    CHECK(a->getFlags() == e->getFlags());
    CHECK(a->getPickedPosition().pos == e->getPickedPosition().pos);
    CHECK(a->getPickedPosition().frame == e->getPickedPosition().frame);
    CHECK(actual->getKeyframeCount(id) == expected->getKeyframeCount(id));
    for (int k = 0; k < expected->getKeyframeCount(id); ++k)
      CHECK(actual->getKeyframe(id, k) == expected->getKeyframe(id, k));
  }
  for (int page = 0; page < expected->getPageCount(); ++page) {
    auto *a = actual->getPage(page);
    auto *e = expected->getPage(page);
    CHECK(a->getName() == e->getName());
    CHECK(a->getStyleCount() == e->getStyleCount());
    for (int index = 0; index < e->getStyleCount(); ++index)
      CHECK(a->getStyleId(index) == e->getStyleId(index));
  }
  for (int key = '0'; key <= '9'; ++key)
    CHECK(actual->getShortcutValue(key) == expected->getShortcutValue(key));
}

class FailingStyle final : public TSolidColorStyle {
public:
  FailingStyle() : TSolidColorStyle(TPixel32(100, 20, 50, 255)) {}
  TColorStyle *clone() const override { return new FailingStyle(*this); }

protected:
  void saveData(TOutputStreamInterface &os) const override {
    os << 100;
    throw TException("Injected palette serialization failure");
  }
};

static void serializationFailurePalette(TPalette *palette) {
  palette->getPage(0)->addStyle(new FailingStyle());
  palette->setDirtyFlag(true);
}

static void constrainedSave(Fixture &f, bool projectSave) {
  const pid_t child = fork();
  CHECK(child >= 0);
  if (child == 0) {
    signal(SIGXFSZ, SIG_IGN);
    struct rlimit limit = {64, 64};
    if (setrlimit(RLIMIT_FSIZE, &limit)) _exit(3);
    try {
      if (projectSave)
        f.fc->savePalette(&f.scene);
      else
        f.fc->saveAsDefault(f.fc->getPalette(&f.scene));
    } catch (const TException &) {
      _exit(f.fc->getDirtyFlag() ? 0 : 4);
    }
    _exit(5);
  }
  int status = 0;
  CHECK(waitpid(child, &status, 0) == child);
  CHECK(WIFEXITED(status));
  CHECK(WEXITSTATUS(status) == 0);
}

int runApplicationTests();

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  TColorStyle::declare(new TSolidColorStyle());
  int passed = 0, failed = 0;
  auto test = [&](const char *name, const std::function<void()> &body) {
    try {
      body();
      ++passed;
      std::cout << "PASS " << name << std::endl;
    } catch (const std::exception &e) {
      ++failed;
      std::cout << "FAIL " << name << ": " << e.what() << std::endl;
    } catch (...) {
      ++failed;
      std::cout << "FAIL " << name << ": unexpected nonstandard exception"
                << std::endl;
    }
  };

  test("missing files yield usable basic raster palette", [] {
    Fixture f;
    CHECK(!f.fc->getDirtyFlag());
    checkBasic(f.fc->getPalette(&f.scene));
    CHECK(!f.fc->getDirtyFlag());
    CHECK(!TSystem::doesExistFileOrLevel(f.current()));
    CHECK(!TSystem::doesExistFileOrLevel(f.global()));
  });

  test("rich default snapshot roundtrip and live source preservation", [] {
    Fixture f;
    auto source     = makePalette();
    auto *style     = source->getStyle(2);
    const int frame = source->getFrame();
    f.fc->saveAsDefault(source.getPointer());
    auto loaded = readRaw(f.global());
    checkContents(loaded.getPointer(), source.getPointer());
    CHECK(loaded->isLocked());
    CHECK(source->getStyle(2) == style);
    CHECK(source->getFrame() == frame);
    CHECK(source->getCurrentStyleId() == 4);
    CHECK(source->getPaletteName() == L"Live source palette");
    CHECK(source->getGlobalName() == L"Source palette global identity");
    CHECK(source->getDirtyFlag());
    CHECK(source->getAskOverwriteFlag());
    CHECK(source->isLocked());
    CHECK(directoryEntries(f.global()) ==
          QStringList{"Raster_Drawing_Palette.tpl"});
    TPalette *cached = f.fc->getPalette(&f.scene);
    checkContents(cached, source.getPointer());
    CHECK(cached->getPaletteName() == L"Raster Drawing Palette");
    CHECK(f.fc->getPalette(&f.scene) == cached);
    const QByteArray snapshot = readBytes(f.global());
    source->getStyle(3)->setMainColor(TPixel32(1, 2, 3, 4));
    CHECK(readBytes(f.global()) == snapshot);
    CHECK(cached->getStyle(3)->getMainColor() !=
          source->getStyle(3)->getMainColor());
  });

  test("global default loads with no scene", [] {
    Fixture f;
    auto source = makePalette(1);
    f.fc->saveAsDefault(source.getPointer());
    checkContents(f.fc->getPalette(nullptr), source.getPointer());
  });

  test("current project palette precedes legacy and global", [] {
    Fixture f;
    auto global  = makePalette(1);
    auto legacy  = makePalette(2);
    auto current = makePalette(3);
    f.fc->saveAsDefault(global.getPointer());
    writeRaw(f.legacy(), legacy.getPointer());
    writeRaw(f.current(), current.getPointer());
    checkContents(f.fc->getPalette(&f.scene), current.getPointer());
  });

  test("legacy project palette precedes global and saves current filename", [] {
    Fixture f;
    auto global = makePalette(1);
    auto legacy = makePalette(2);
    f.fc->saveAsDefault(global.getPointer());
    writeRaw(f.legacy(), legacy.getPointer());
    const QByteArray originalLegacy = readBytes(f.legacy());
    auto *loaded                    = f.fc->getPalette(&f.scene);
    checkContents(loaded, legacy.getPointer());
    loaded->setDirtyFlag(true);
    f.fc->savePalette(&f.scene);
    CHECK(!f.fc->getDirtyFlag());
    CHECK(TSystem::doesExistFileOrLevel(f.current()));
    CHECK(readBytes(f.legacy()) == originalLegacy);
    checkContents(readRaw(f.current()).getPointer(), legacy.getPointer());
  });

  test("missing project files fall back to global", [] {
    Fixture f;
    auto source = makePalette(4);
    f.fc->saveAsDefault(source.getPointer());
    checkContents(f.fc->getPalette(&f.scene), source.getPointer());
    CHECK(!TSystem::doesExistFileOrLevel(f.current()));
  });

  test("missing palettes alias falls back to global", [] {
    Fixture f;
    auto global  = makePalette(5);
    auto project = makePalette(6);
    f.fc->saveAsDefault(global.getPointer());
    writeRaw(f.current(), project.getPointer());
    f.scene.project.palettesAlias = false;
    checkContents(f.fc->getPalette(&f.scene), global.getPointer());
  });

  test("empty palette saves create a project file and clear dirty state", [] {
    Fixture f;
    auto *palette = f.fc->getPalette(&f.scene);
    palette->getPage(0)->addStyle(TPixel32(10, 20, 30, 40));
    palette->setDirtyFlag(true);
    CHECK(f.fc->getDirtyFlag());
    f.fc->savePalette(&f.scene);
    CHECK(!f.fc->getDirtyFlag());
    CHECK(!palette->getAskOverwriteFlag());
    auto saved = readRaw(f.current());
    CHECK(saved->getStyleCount() == 3);
    CHECK(saved->getStyle(2)->getMainColor() == TPixel32(10, 20, 30, 40));
  });

  test("clean palette save does not create files", [] {
    Fixture f;
    f.fc->savePalette(nullptr);
    f.fc->getPalette(&f.scene);
    f.fc->savePalette(&f.scene);
    CHECK(!TSystem::doesExistFileOrLevel(f.current()));
  });

  test("default save of live raster palette preserves dirty state", [] {
    Fixture f;
    auto *palette = f.fc->getPalette(&f.scene);
    palette->getPage(0)->addStyle(TPixel32(55, 66, 77, 88));
    palette->setDirtyFlag(true);
    f.fc->saveAsDefault(palette);
    CHECK(f.fc->getDirtyFlag());
    CHECK(palette->getAskOverwriteFlag());
    CHECK(readRaw(f.global())->getStyle(2)->getMainColor() ==
          TPixel32(55, 66, 77, 88));
  });

  test("null default save is harmless", [] {
    Fixture f;
    f.fc->saveAsDefault(nullptr);
    CHECK(!TSystem::doesExistFileOrLevel(f.global()));
  });

  test("missing project or palettes alias failures retain dirty edits", [] {
    Fixture f;
    auto *palette = f.fc->getPalette(&f.scene);
    palette->setDirtyFlag(true);
    expectThrow([&] { f.fc->savePalette(nullptr); });
    CHECK(f.fc->getDirtyFlag());
    CHECK(palette->getAskOverwriteFlag());
    f.scene.project.palettesAlias = false;
    expectThrow([&] { f.fc->savePalette(&f.scene); });
    CHECK(f.fc->getDirtyFlag());
    CHECK(!TSystem::doesExistFileOrLevel(f.current()));
  });

  test("malformed global file yields usable basic palette", [] {
    Fixture f;
    writeBytes(f.global(), "not a palette\n");
    checkBasic(f.fc->getPalette(&f.scene));
  });

  test("truncated closing tag yields usable basic palette", [] {
    Fixture f;
    auto source = makePalette();
    f.fc->saveAsDefault(source.getPointer());
    QByteArray bytes = readBytes(f.global());
    const int close  = bytes.lastIndexOf("</palette>");
    CHECK(close > 0);
    bytes.truncate(close);
    writeBytes(f.global(), bytes);
    checkBasic(f.fc->getPalette(&f.scene));
  });

  test("missing base style pages yields usable basic palette", [] {
    Fixture f;
    auto source = makePalette();
    source->getPage(0)->removeStyle(0);
    f.fc->saveAsDefault(source.getPointer());
    checkBasic(f.fc->getPalette(&f.scene));
  });

  test("malformed current palette is isolated from valid global", [] {
    Fixture f;
    auto source = makePalette();
    f.fc->saveAsDefault(source.getPointer());
    writeBytes(f.current(), "<wrong></wrong>\n");
    checkBasic(f.fc->getPalette(&f.scene));
  });

  test("serialization failure preserves existing default and source", [] {
    Fixture f;
    auto source = makePalette();
    f.fc->saveAsDefault(source.getPointer());
    const QByteArray previous = readBytes(f.global());
    const QStringList entries = directoryEntries(f.global());
    serializationFailurePalette(source.getPointer());
    expectThrow([&] { f.fc->saveAsDefault(source.getPointer()); });
    CHECK(readBytes(f.global()) == previous);
    CHECK(directoryEntries(f.global()) == entries);
    CHECK(source->getDirtyFlag());
    CHECK(source->isLocked());
    CHECK(source->getCurrentStyleId() == 4);
  });

  test("serialization failure preserves project file and dirty edits", [] {
    Fixture f;
    auto *palette = f.fc->getPalette(&f.scene);
    palette->setDirtyFlag(true);
    f.fc->savePalette(&f.scene);
    const QByteArray previous = readBytes(f.current());
    const QStringList entries = directoryEntries(f.current());
    serializationFailurePalette(palette);
    expectThrow([&] { f.fc->savePalette(&f.scene); });
    CHECK(readBytes(f.current()) == previous);
    CHECK(directoryEntries(f.current()) == entries);
    CHECK(f.fc->getDirtyFlag());
    CHECK(palette->getAskOverwriteFlag());
  });

  test("real short write preserves existing default and dirty edits", [] {
    Fixture f;
    auto *palette = f.fc->getPalette(&f.scene);
    f.fc->saveAsDefault(palette);
    const QByteArray previous = readBytes(f.global());
    const QStringList entries = directoryEntries(f.global());
    palette->getPage(0)->addStyle(TPixel32(99, 88, 77, 66));
    palette->setDirtyFlag(true);
    constrainedSave(f, false);
    CHECK(readBytes(f.global()) == previous);
    CHECK(directoryEntries(f.global()) == entries);
  });

  test("real short write preserves existing project and dirty edits", [] {
    Fixture f;
    auto *palette = f.fc->getPalette(&f.scene);
    palette->setDirtyFlag(true);
    f.fc->savePalette(&f.scene);
    const QByteArray previous = readBytes(f.current());
    const QStringList entries = directoryEntries(f.current());
    palette->getPage(0)->addStyle(TPixel32(88, 77, 66, 55));
    palette->setDirtyFlag(true);
    constrainedSave(f, true);
    CHECK(readBytes(f.current()) == previous);
    CHECK(directoryEntries(f.current()) == entries);
  });

  test(
      "destination directory failures retain dirty state and clean temporaries",
      [] {
        Fixture f;
        auto *palette = f.fc->getPalette(&f.scene);
        palette->setDirtyFlag(true);
        CHECK(QDir().mkpath(f.current().getQString()));
        writeBytes(f.current() + "sentinel", "preserve me");
        const QStringList entries = directoryEntries(f.current());
        expectThrow([&] { f.fc->savePalette(&f.scene); });
        CHECK(f.fc->getDirtyFlag());
        CHECK(readBytes(f.current() + "sentinel") == "preserve me");
        CHECK(directoryEntries(f.current()) == entries);
        CHECK(QDir().mkpath(f.global().getQString()));
        writeBytes(f.global() + "sentinel", "preserve global");
        const QStringList defaults = directoryEntries(f.global());
        expectThrow([&] { f.fc->saveAsDefault(palette); });
        CHECK(readBytes(f.global() + "sentinel") == "preserve global");
        CHECK(directoryEntries(f.global()) == defaults);
        CHECK(f.fc->getDirtyFlag());
      });

  test("loaded raster palette preserves serialized lock", [] {
    Fixture f;
    auto source = makePalette();
    writeRaw(f.current(), source.getPointer());
    CHECK(readRaw(f.current())->isLocked());
    CHECK(f.fc->getPalette(&f.scene)->isLocked());
  });

  test("loaded raster palette preserves reference image frame selection", [] {
    Fixture f;
    auto source                        = makePalette();
    const std::vector<TFrameId> frames = {TFrameId(2), TFrameId(8)};
    source->setRefImgPath(TFilePath("reference..png"));
    source->setRefLevelFids(frames, true);
    writeRaw(f.current(), source.getPointer());
    CHECK(readRaw(f.current())->getRefLevelFids() == frames);
    CHECK(f.fc->getPalette(&f.scene)->getRefLevelFids() == frames);
  });

  test("saved default snapshot preserves reference image frame selection", [] {
    Fixture f;
    auto source                        = makePalette();
    const std::vector<TFrameId> frames = {TFrameId(2), TFrameId(8)};
    source->setRefImgPath(TFilePath("reference..png"));
    source->setRefLevelFids(frames, true);
    f.fc->saveAsDefault(source.getPointer());
    CHECK(source->getRefLevelFids() == frames);
    CHECK(readRaw(f.global())->getRefLevelFids() == frames);
  });

  test(
      "raster edits and defaults leave indexed default palette files unchanged",
      [] {
        Fixture f;
        const TFilePath folder      = f.global().getParentDir();
        const TFilePath toonzRaster = folder + "Toonz_Raster_Palette.tpl";
        const TFilePath vector      = folder + "Toonz_Vector_Palette.tpl";
        auto indexed                = makePalette(7);
        writeRaw(toonzRaster, indexed.getPointer());
        writeRaw(vector, indexed.getPointer());
        const QByteArray inkBytes    = readBytes(toonzRaster);
        const QByteArray vectorBytes = readBytes(vector);
        auto *raster                 = f.fc->getPalette(&f.scene);
        raster->getPage(0)->addStyle(TPixel32(12, 34, 56, 255));
        raster->setDirtyFlag(true);
        f.fc->savePalette(&f.scene);
        f.fc->saveAsDefault(raster);
        f.fc->clear();
        CHECK(f.fc->getPalette(&f.scene)->getStyleCount() == 3);
        CHECK(readBytes(toonzRaster) == inkBytes);
        CHECK(readBytes(vector) == vectorBytes);
      });

  std::cout << passed << " test groups passed; " << failed << " failed; "
            << assertions << " assertions" << std::endl;
  const int applicationFailures = runApplicationTests();
  return failed || applicationFailures ? 1 : 0;
}
