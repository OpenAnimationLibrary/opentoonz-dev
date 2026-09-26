#include "../configtransfer.h"
#include "stubs/testenvironment.h"
#include "stubs/toonz/preferences.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

namespace {

void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

QString file(const QString &relative) {
  return TestEnvironment::under(relative);
}

void write(const QString &name, const QByteArray &data) {
  check(QDir().mkpath(QFileInfo(name).absolutePath()),
        "cannot create fixture directory");
  QFile output(name);
  check(output.open(QIODevice::WriteOnly), "cannot create fixture file");
  check(output.write(data) == data.size(), "incomplete fixture write");
}

QByteArray read(const QString &name) {
  QFile input(name);
  check(input.open(QIODevice::ReadOnly), "cannot read restored file");
  return input.readAll();
}

const ConfigTransfer::Item &item(const QList<ConfigTransfer::Item> &items,
                                 const QString &archivePath) {
  for (const auto &candidate : items)
    if (candidate.archivePath == archivePath) return candidate;
  throw std::runtime_error("archive item missing from preview");
}

void sourceProfile() {
  write(file("profiles/layouts/settings.alice/preferences.ini"),
        "RecognizedOption=from-source\nLocalPath=C:/source\n"
        "RemovedOption=obsolete\n[levelFormats]\nsize=1\n"
        "1\\name=Custom PNG\n1\\regexp=.*\\.png\n"
        "1\\priority=10\n1\\antialias=40\n1\\removedField=old\n");
  write(file("profiles/layouts/settings.alice/shortcuts.ini"),
        "[shortcuts]\nMI_Open=Ctrl+M\nMI_Room_Drawing=Alt+1\n"
        "MI_Gone=Ctrl+G\n");
  write(file("profiles/layouts/personal/Default.alice/layouts.txt"),
        "room1.ini\n");
  write(file("profiles/layouts/personal/Default.alice/studio_layout.txt"),
        "room1.ini\n");
  write(file("profiles/layouts/personal/Default.alice/room1.ini"),
        "[room]\nname=Drawing\nhierarchy=0\n"
        "pane_0\\name=SceneViewer\n");
  write(file("profiles/layouts/personal/Default.alice/room1_menubar.xml"),
        "<menubar><menu/></menubar>");
  write(file("profiles/layouts/personal/Default.alice/currentRoom.txt"),
        "Drawing");
  write(file("config/brush_vector.txt"), "custom brush");
  write(file("profiles/env/alice.env"),
        "RecognizedOption \"from-source\"\n"
        "RemovedOption \"obsolete\"\n");
  write(file("library/mypaint brushes/Custom/brush.myb"), "brush data");
  write(file("library/mypaint brushes/Custom/new.myb"), "new brush");
  write(file("plugins/example/manifest.json"), "{\"version\":\"1.0\"}");
  write(file("plugins/example/example.dll"), "old plugin");
  write(file("plugins/upgrade/manifest.json"), "{\"version\":\"3.0\"}");
  write(file("plugins/upgrade/upgrade.dll"), "upgraded plugin");
}

void destinationProfile() {
  write(file("profiles/layouts/settings.bob/preferences.ini"),
        "RecognizedOption=from-destination\nLocalPath=D:/local\n"
        "OnlyDestination=preserved\n[levelFormats]\nsize=1\n"
        "1\\name=Old Format\n1\\regexp=.*\\.tga\n");
  write(file("profiles/layouts/settings.bob/shortcuts.ini"),
        "[shortcuts]\nMI_Open=Ctrl+O\nMI_Local=F5\n");
  write(file("profiles/env/bob.env"),
        "RecognizedOption \"from-destination\"\n"
        "OnlyDestination \"preserved\"\n");
  write(file("plugins/example/manifest.json"), "{\"version\":\"2.0\"}");
  write(file("plugins/example/example.dll"), "newer plugin");
  write(file("plugins/upgrade/manifest.json"), "{\"version\":\"2.0\"}");
  write(file("plugins/upgrade/upgrade.dll"), "older plugin");
  write(file("library/mypaint brushes/Custom/brush.myb"), "local brush");
}

void roundTrip(const QString &scratch) {
  Preferences::instance()->m_items.insert(
      0, {"RecognizedOption", QMetaType::QString, {}, {}});
  Preferences::instance()->m_items.insert(
      1, {"LocalPath", QMetaType::QString, {}, {}});

  TestEnvironment::root() = QDir(scratch).filePath("source");
  TestEnvironment::user() = "alice";
  sourceProfile();
  const QString archive = QDir(scratch).filePath("configuration.otconfig");
  ConfigTransfer::Options options;
  options.library = options.plugins = true;
  QString error;
  const bool saved = ConfigTransfer::save(archive, options, error);
  check(saved, qPrintable("save failed: " + error));

  TestEnvironment::root() = QDir(scratch).filePath("destination");
  TestEnvironment::user() = "bob";
  destinationProfile();
  QList<ConfigTransfer::Item> items;
  const bool inspected = ConfigTransfer::inspect(archive, items, error);
  check(inspected, qPrintable("inspect failed: " + error));
  const auto &room = item(items, "files/rooms/Default/room1.ini");
  check(room.selected && room.destination.endsWith("Default.bob/room1.ini"),
        "personal room did not map to destination user");
  check(item(items, "files/settings/preferences.ini").selected,
        "different preferences were not selected");
  const auto &plugin = item(items, "files/plugins/example/example.dll");
  check(plugin.existing && !plugin.selected &&
            plugin.status.contains("Installed newer"),
        "newer installed plugin was not protected");
  check(
      !item(items, "files/library/mypaint brushes/Custom/brush.myb").selected &&
          item(items, "files/library/mypaint brushes/Custom/new.myb").selected,
      "missing MyPaint brush was not selected");
  for (auto &candidate : items)
    if (candidate.category == "env" ||
        candidate.archivePath.startsWith("files/plugins/upgrade/"))
      candidate.selected = true;
  check(item(items, "files/plugins/upgrade/upgrade.dll")
            .status.contains("Archive newer"),
        "newer archived plugin was not identified");
  const bool staged = ConfigTransfer::queueRestore(archive, items, error);
  check(staged, qPrintable("staging failed: " + error));
  check(QFileInfo::exists(file("config/config-restore-transfer/pending.json")),
        "restore was not staged");
  const bool applied = ConfigTransfer::applyPending(error);
  check(applied, qPrintable("startup restore failed: " + error));
  check(!QFileInfo::exists(file("config/config-restore-transfer/pending.json")),
        "applied restore remains pending");

  QSettings preferences(file("profiles/layouts/settings.bob/preferences.ini"),
                        QSettings::IniFormat);
  check(preferences.value("RecognizedOption").toString() == "from-source",
        "recognized preference did not migrate");
  check(preferences.value("LocalPath").toString() == "D:/local" &&
            preferences.value("OnlyDestination").toString() == "preserved" &&
            !preferences.contains("RemovedOption"),
        "migration replaced local or removed preferences");
  check(preferences.value("levelFormats/1/name").toString() == "Custom PNG" &&
            preferences.value("levelFormats/1/antialias").toInt() == 40 &&
            !preferences.contains("levelFormats/1/removedField"),
        "user level formats did not migrate");
  QSettings shortcuts(file("profiles/layouts/settings.bob/shortcuts.ini"),
                      QSettings::IniFormat);
  check(
      shortcuts.value("shortcuts/MI_Open").toString() == "Ctrl+M" &&
          shortcuts.value("shortcuts/MI_Room_Drawing").toString() == "Alt+1" &&
          shortcuts.value("shortcuts/MI_Local").toString() == "F5" &&
          !shortcuts.contains("shortcuts/MI_Gone"),
      "shortcut compatibility merge failed");
  check(read(file("profiles/layouts/personal/Default.bob/room1.ini")) ==
                "[room]\nname=Drawing\nhierarchy=0\n"
                "pane_0\\name=SceneViewer\n" &&
            read(file("profiles/layouts/personal/Default.bob/layouts.txt")) ==
                "room1.ini\n" &&
            read(file(
                "profiles/layouts/personal/Default.bob/studio_layout.txt")) ==
                "room1.ini\n",
        "room set did not survive restore");
  check(
      read(file("config/brush_vector.txt")) == "custom brush" &&
          read(file("library/mypaint brushes/Custom/brush.myb")) ==
              "local brush" &&
          read(file("library/mypaint brushes/Custom/new.myb")) == "new brush" &&
          read(file("plugins/example/example.dll")) == "newer plugin" &&
          read(file("plugins/upgrade/upgrade.dll")) == "upgraded plugin",
      "config, library, or plugin restore policy failed");
  check(read(file("profiles/env/bob.env")).contains("from-source") &&
            !read(file("profiles/env/bob.env")).contains("RemovedOption"),
        "environment migration failed");

  // A newly changed local file must not be silently replaced at startup,
  // even when the user chose its previous version in the preview.
  QList<ConfigTransfer::Item> conflictItems;
  error.clear();
  const bool secondInspected =
      ConfigTransfer::inspect(archive, conflictItems, error);
  check(secondInspected, qPrintable("second inspect failed: " + error));
  for (auto &candidate : conflictItems)
    candidate.selected = candidate.archivePath ==
                         "files/library/mypaint brushes/Custom/brush.myb";
  const bool secondStaged =
      ConfigTransfer::queueRestore(archive, conflictItems, error);
  check(secondStaged, qPrintable("second staging failed: " + error));
  write(file("library/mypaint brushes/Custom/brush.myb"),
        "edited after staging");
  error.clear();
  check(!ConfigTransfer::applyPending(error) && error.contains("changed"),
        "startup ignored a changed optional destination");
  check(read(file("library/mypaint brushes/Custom/brush.myb")) ==
            "edited after staging",
        "changed library file was overwritten");
}

}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    QTemporaryDir scratch;
    check(scratch.isValid(), "cannot create test directory");
    roundTrip(scratch.path());
  } catch (const std::exception &failure) {
    std::cerr << failure.what() << '\n';
    return 1;
  }
  return 0;
}
